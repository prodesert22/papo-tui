// Development-only HTTPS/WSS proxy. TLS and upgrades use Go's standard library.
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

func main() {
	upstream := flag.String("upstream", "", "Loopback HTTP backend")
	cert := flag.String("cert", "", "TLS certificate")
	key := flag.String("key", "", "TLS private key")
	messageDelay := flag.Duration("message-delay", 0, "Development delay before a message response")
	dropFirst := flag.Bool("drop-first-message-response", false, "Discard the first successful message response after creation")
	delayHistory := flag.Int("delay-history-ms", 0, "Controlled delay of GET history responses (0..5000ms)")
	r7Control := flag.Bool("r7-control", false, "Enable loopback-only test network controls")
	flag.Parse()
	if *delayHistory < 0 || *delayHistory > 5000 {
		os.Exit(1)
	}
	target, err := url.Parse(*upstream)
	if err != nil || target.Scheme != "http" || target.Hostname() != "127.0.0.1" || target.User != nil {
		os.Exit(1)
	}
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		os.Exit(1)
	}
	proxy := httputil.NewSingleHostReverseProxy(target)
	proxy.ErrorLog = log.New(io.Discard, "", 0)
	var dropped atomic.Bool
	proxy.ModifyResponse = func(response *http.Response) error {
		request := response.Request
		if *delayHistory > 0 && request.Method == http.MethodGet &&
			strings.HasPrefix(request.URL.Path, "/channels/") && strings.HasSuffix(request.URL.Path, "/messages") {
			timer := time.NewTimer(time.Duration(*delayHistory) * time.Millisecond)
			defer timer.Stop()
			select {
			case <-timer.C:
			case <-request.Context().Done():
				return request.Context().Err()
			}
		}
		if response.Request.Method == http.MethodPost && response.Request.URL.Path == "/messages" && response.StatusCode == http.StatusCreated {
			if *messageDelay > 0 {
				time.Sleep(*messageDelay)
			}
			if *dropFirst && dropped.CompareAndSwap(false, true) {
				response.Body.Close()
				return errors.New("development response intentionally discarded")
			}
		}
		return nil
	}
	var offline atomic.Bool
	var connections sync.Map
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if *r7Control && r.Method == http.MethodPost && (r.URL.Path == "/__papo_test/offline" || r.URL.Path == "/__papo_test/online") {
			offline.Store(r.URL.Path == "/__papo_test/offline")
			if offline.Load() {
				connections.Range(func(key, value any) bool {
					key.(net.Conn).Close()
					connections.Delete(key)
					return true
				})
			}
			w.WriteHeader(http.StatusNoContent)
			return
		}
		if offline.Load() {
			w.WriteHeader(http.StatusServiceUnavailable)
			return
		}
		proxy.ServeHTTP(w, r)
	})
	server := &http.Server{Handler: handler, ReadHeaderTimeout: 5 * time.Second, ErrorLog: log.New(io.Discard, "", 0),
		ConnState: func(c net.Conn, state http.ConnState) {
			if state == http.StateHijacked {
				connections.Store(c, true)
			}
			if state == http.StateClosed {
				connections.Delete(c)
			}
		},
	}
	fmt.Println("https://" + listener.Addr().String())
	if server.ServeTLS(listener, *cert, *key) != nil {
		os.Exit(1)
	}
}
