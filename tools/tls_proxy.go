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
	server := &http.Server{Handler: proxy, ReadHeaderTimeout: 5 * time.Second, ErrorLog: log.New(io.Discard, "", 0)}
	fmt.Println("https://" + listener.Addr().String())
	if server.ServeTLS(listener, *cert, *key) != nil {
		os.Exit(1)
	}
}
