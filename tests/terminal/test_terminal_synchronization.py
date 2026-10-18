"""Exercise real editor scenarios without observing output inside send()."""

import unittest
import importlib
import os
from pathlib import Path
import shutil
import sys
from unittest.mock import patch

import test_input_controls
import test_message_interactions
import test_footer_terminal
import test_terminal as terminal_support
from test_terminal import Terminal, TerminalStream


class TerminalSynchronization(unittest.TestCase):
    def exercise(self, case_type, method):
        send = Terminal.send
        resize = Terminal.resize
        feed = TerminalStream.feed

        def deferred_send(terminal, value):
            # Input delivery succeeds, but its frame is only observed by an
            # explicit wait. Increasing send()'s fixed sleep cannot fix this.
            with patch.object(terminal, "read"):
                send(terminal, value)

        def deferred_resize(terminal, rows, columns):
            with patch.object(terminal, "read"):
                resize(terminal, rows, columns)

        for chunk_size in (None, 7):
            with self.subTest(method=method, chunk_size=chunk_size):

                def fragmented_feed(stream, data):
                    for offset in range(0, len(data), chunk_size or len(data) or 1):
                        feed(stream, data[offset : offset + (chunk_size or len(data))])

                with (
                    patch.object(Terminal, "send", deferred_send),
                    patch.object(Terminal, "resize", deferred_resize),
                    patch.object(TerminalStream, "feed", fragmented_feed),
                ):
                    result = unittest.TestResult()
                    case_type(method).run(result)
                self.assertTrue(result.wasSuccessful(), str(result.failures + result.errors))

    def test_input_controls_wait_for_text_cursor_selection_and_clipboard(self):
        for method in unittest.defaultTestLoader.getTestCaseNames(
            test_input_controls.InputControls
        ):
            self.exercise(test_input_controls.InputControls, method)

    def test_canceling_reaction_waits_for_emoji_completion(self):
        self.exercise(
            test_message_interactions.MessageInteractions,
            "test_canceling_reaction_picker_restores_ordinary_emoji_completion",
        )

    def test_reaction_columns_wait_for_scroll_results(self):
        self.exercise(
            test_message_interactions.MessageInteractions,
            "test_participant_columns_select_filter_and_scroll_independently",
        )

    def test_message_menu_waits_for_dismissal_before_clicking(self):
        self.exercise(
            test_message_interactions.MessageInteractions,
            "test_f7_selects_messages_and_context_menu_is_available_directly",
        )

    def test_footer_waits_for_resize(self):
        for method in unittest.defaultTestLoader.getTestCaseNames(
            test_footer_terminal.FooterTerminal
        ):
            self.exercise(test_footer_terminal.FooterTerminal, method)

    def test_login_waits_for_cursor_and_resize(self):
        for method in (
            "test_three_areas_and_wide_text_survive_resize",
            "test_repeated_backspace_stays_in_each_field",
            "test_mouse_focus_fields_login_and_logout_after_resize",
        ):
            self.exercise(terminal_support.TerminalTests, method)

    def test_media_panels_wait_for_close_in_each_graphics_protocol(self):
        ffmpeg = os.environ.get("PAPO_TEST_FFMPEG") or shutil.which("ffmpeg")
        self.assertTrue(ffmpeg, "FFmpeg is required for terminal media scenarios")
        with patch.object(sys, "argv", [sys.argv[0], terminal_support.BINARY, ffmpeg]):
            media = importlib.import_module("test_media_terminal")
        media.MediaTerminal.setUpClass()
        try:
            for method in (
                "test_visible_video_has_automatic_static_poster_and_reuses_download",
                "test_modal_fits_small_embed_and_properties_reuses_download",
            ):
                self.exercise(media.MediaTerminal, method)
        finally:
            media.MediaTerminal.tearDownClass()

    def test_media_properties_wait_for_keys_clicks_and_resize(self):
        modal = importlib.import_module("test_media_modal")
        probe = os.environ.get("PAPO_TEST_MODAL_PROBE") or str(
            Path(terminal_support.BINARY).with_name("media-modal-probe")
        )
        with patch.object(terminal_support, "BINARY", probe):
            self.exercise(modal.ModalTest, "test_properties_button_keys_resize_and_outside_click")


if __name__ == "__main__":
    unittest.main(verbosity=2)
