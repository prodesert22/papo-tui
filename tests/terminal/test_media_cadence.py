"""Fast video and GIF playback must advance without starving terminal input."""

from test_context import main as test_main, protocols
from terminal_media import FFMPEG, MediaTerminal
from media_cadence import measure


class Cadence(MediaTerminal):
    def test_fast_gif_follows_source_timing(self):
        for protocol in protocols("kitty", "sixel", "iterm2", "chafa", "light"):
            with self.subTest(protocol=protocol):
                report = measure(self, FFMPEG, video=False, protocol=protocol)
                self.assertGreater(report["updates_per_second"], 22, report)
                self.assertGreater(report["distinct_states"], 20, report)
                self.assertLess(report["keyboard_ms"], 500, report)
                if protocol == "kitty":
                    self.assertLess(report["terminal_bytes"], report["raster_bytes"] * 0.75, report)

    def test_fast_video_and_keyboard_remain_responsive(self):
        for protocol in protocols("kitty", "chafa"):
            with self.subTest(protocol=protocol):
                report = measure(self, FFMPEG, video=True, protocol=protocol)
                self.assertGreater(report["updates_per_second"], 35, report)
                self.assertGreater(report["distinct_states"], 20, report)
                self.assertLess(report["keyboard_ms"], 500, report)
                if protocol == "kitty":
                    self.assertLess(report["terminal_bytes"], report["raster_bytes"] * 0.75, report)


if __name__ == "__main__":
    test_main(2)
