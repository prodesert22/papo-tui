#include "voice_devices.h"
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static gint audible;
typedef struct {
    GstDevice parent;
    bool input;
} TestVoiceDevice;
typedef struct {
    GstDeviceClass parent;
} TestVoiceDeviceClass;
G_DEFINE_TYPE(TestVoiceDevice, test_voice_device, GST_TYPE_DEVICE)
static void handoff(GstElement *sink, GstBuffer *buffer, GstPad *pad, gpointer data)
{
    (void)sink;
    (void)data;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GstAudioInfo info;
    gst_audio_info_init(&info);
    GstMapInfo map;
    bool loud = false;
    if (caps && gst_audio_info_from_caps(&info, caps) &&
        gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        size_t count = MIN(map.size / GST_AUDIO_INFO_BPS(&info), 4096);
        double energy = 0;
        for (size_t i = 0; i < count; ++i) {
            double sample = 0;
            if (GST_AUDIO_INFO_FORMAT(&info) == GST_AUDIO_FORMAT_F32LE) {
                float value;
                memcpy(&value, map.data + i * sizeof(float), sizeof(float));
                sample = value;
            } else if (GST_AUDIO_INFO_FORMAT(&info) == GST_AUDIO_FORMAT_S16LE) {
                gint16 value;
                memcpy(&value, map.data + i * sizeof(value), sizeof(value));
                sample = value / 32768.0;
            } else if (GST_AUDIO_INFO_FORMAT(&info) == GST_AUDIO_FORMAT_F64LE) {
                memcpy(&sample, map.data + i * sizeof(double), sizeof(double));
            }
            energy += sample * sample;
        }
        loud = count && energy / count > 0.00001;
        gst_buffer_unmap(buffer, &map);
    }
    if (caps)
        gst_caps_unref(caps);
    if (loud)
        g_atomic_int_inc(&audible);
}
/* Physical microphones negotiate caps a moment after PLAYING; emulate that in
 * the source's own streaming thread so callers cannot rely on instant caps. */
static GstPadProbeReturn late_caps(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void)pad;
    (void)data;
    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_CAPS)
        g_usleep(300000);
    return GST_PAD_PROBE_OK;
}
static GstElement *create_element(GstDevice *device, const gchar *name)
{
    TestVoiceDevice *d = (TestVoiceDevice *)device;
    GstElement *element = gst_element_factory_make(d->input ? "audiotestsrc" : "fakesink", name);
    if (d->input) {
        g_object_set(element, "is-live", true, "freq", 440.0, "volume", 0.2, NULL);
        GstPad *pad = gst_element_get_static_pad(element, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, late_caps, NULL, NULL);
        gst_object_unref(pad);
    } else {
        g_object_set(element, "sync", true, "signal-handoffs", true, NULL);
        g_signal_connect(element, "handoff", G_CALLBACK(handoff), NULL);
    }
    return element;
}
static void test_voice_device_class_init(TestVoiceDeviceClass *klass)
{
    GST_DEVICE_CLASS(klass)->create_element = create_element;
}
static void test_voice_device_init(TestVoiceDevice *device)
{
    (void)device;
}
typedef struct {
    GstDeviceProvider parent;
} TestVoiceProvider;
typedef struct {
    GstDeviceProviderClass parent;
} TestVoiceProviderClass;
G_DEFINE_TYPE(TestVoiceProvider, test_voice_provider, GST_TYPE_DEVICE_PROVIDER)
static GList *probe(GstDeviceProvider *provider)
{
    (void)provider;
    GList *result = NULL;
    GstCaps *caps = gst_caps_from_string("audio/x-raw");
    for (int i = 0; i < 2; ++i) {
        GstStructure *properties =
            gst_structure_new("papo-test", "fixture", G_TYPE_STRING, i ? "sink" : "source", NULL);
        TestVoiceDevice *device = g_object_new(test_voice_device_get_type(), "display-name",
                                               i ? "Papo synthetic output" : "Papo synthetic input",
                                               "device-class", i ? "Audio/Sink" : "Audio/Source",
                                               "caps", caps, "properties", properties, NULL);
        device->input = !i;
        result = g_list_append(result, device);
        gst_structure_free(properties);
    }
    gst_caps_unref(caps);
    return result;
}
static void test_voice_provider_class_init(TestVoiceProviderClass *klass)
{
    GstDeviceProviderClass *provider = GST_DEVICE_PROVIDER_CLASS(klass);
    provider->probe = probe;
    gst_device_provider_class_set_static_metadata(provider, "Papo test audio", "Audio/Source/Sink",
                                                  "Synthetic audio; no hardware", "Papo tests");
}
static void test_voice_provider_init(TestVoiceProvider *provider)
{
    (void)provider;
}
void test_voice_devices_register(void)
{
    gst_init(NULL, NULL);
    g_assert(gst_device_provider_register(NULL, "papotestaudio", GST_RANK_PRIMARY + 100,
                                          test_voice_provider_get_type()));
}
gint test_voice_audible_buffers(void)
{
    return g_atomic_int_get(&audible);
}
void test_voice_devices_select(audio_config *config, const json_t *devices)
{
    size_t i;
    json_t *device;
    json_array_foreach(devices, i, device)
    {
        const char *name = json_string_value(json_object_get(device, "name")),
                   *id = json_string_value(json_object_get(device, "id"));
        if (name && id && g_str_has_prefix(name, "Papo synthetic"))
            snprintf(json_is_true(json_object_get(device, "input")) ? config->input
                                                                    : config->output,
                     AUDIO_DEVICE_BYTES, "%s", id);
    }
    g_assert(*config->input && *config->output);
}
