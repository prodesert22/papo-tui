"""Exercise real editor scenarios without observing output inside send()."""

from test_context import main as test_main

import unittest
import importlib
import os
from pathlib import Path
import shutil
from unittest.mock import patch

import test_input_controls
import test_message_interactions
import test_footer_terminal
import test_terminal as terminal_cases
import terminal_support
from terminal_support import Terminal, TerminalStream


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

    def test_search_filters_wait_for_each_changed_control(self):
        discovery = importlib.import_module("test_discovery_terminal")
        self.exercise(
            discovery.DiscoveryTerminal,
            "test_filter_controls_selectors_false_attachments_dates_and_cancel",
        )

    def test_input_placeholder_growth_five_rows_wheel_and_clear(self):
        self.exercise(
            test_input_controls.InputControls, "test_placeholder_growth_five_rows_wheel_and_clear"
        )

    def test_input_shift_enter_inserts_a_line_without_sending(self):
        self.exercise(
            test_input_controls.InputControls, "test_shift_enter_inserts_a_line_without_sending"
        )

    def test_input_disambiguated_keypad_keeps_editing_and_enter_shortcuts(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_disambiguated_keypad_keeps_editing_and_enter_shortcuts",
        )

    def test_input_shift_enter_with_lock_modifiers_never_submits(self):
        self.exercise(
            test_input_controls.InputControls, "test_shift_enter_with_lock_modifiers_never_submits"
        )

    def test_input_shift_enter_does_not_submit_the_login_form(self):
        self.exercise(
            test_input_controls.InputControls, "test_shift_enter_does_not_submit_the_login_form"
        )

    def test_input_negotiated_keyboard_mode_is_popped_once_on_sigterm(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_negotiated_keyboard_mode_is_popped_once_on_sigterm",
        )

    def test_input_shift_selection_colors_graphemes_and_word_movement(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_shift_selection_colors_graphemes_and_word_movement",
        )

    def test_input_word_arrows_stop_at_word_edges_in_every_input(self):
        self.exercise(
            test_input_controls.InputControls, "test_word_arrows_stop_at_word_edges_in_every_input"
        )

    def test_input_kitty_copy_marker_lifecycle_and_forwarded_shortcut(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_kitty_copy_marker_lifecycle_and_forwarded_shortcut",
        )

    def test_input_drag_batch_copy_and_expiring_independent_notice(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_drag_batch_copy_and_expiring_independent_notice",
        )

    def test_input_fragmented_paste_is_atomic_keeps_ws_and_never_submits(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_fragmented_paste_is_atomic_keeps_ws_and_never_submits",
        )

    def test_input_oversized_paste_preserves_selection_and_original_draft(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_oversized_paste_preserves_selection_and_original_draft",
        )

    def test_input_login_shared_selection_clear_paste_and_private_password_copy(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_login_shared_selection_clear_paste_and_private_password_copy",
        )

    def test_input_profile_fields_use_words_selection_clear_and_copy(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_profile_fields_use_words_selection_clear_and_copy",
        )

    def test_input_native_clipboard_is_polled_and_owns_the_selected_snapshot(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_native_clipboard_is_polled_and_owns_the_selected_snapshot",
        )

    def test_input_native_clipboard_timeout_falls_back_without_blocking_input(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_native_clipboard_timeout_falls_back_without_blocking_input",
        )

    def test_input_selection_survives_pending_confirmation_and_channel_drafts(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_selection_survives_pending_confirmation_and_channel_drafts",
        )

    def test_input_drag_multiline_and_visual_home_end_do_not_switch_focus(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_drag_multiline_and_visual_home_end_do_not_switch_focus",
        )

    def test_input_copy_shortcut_preserves_selection_and_clears_only_on_ctrl_c(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_copy_shortcut_preserves_selection_and_clears_only_on_ctrl_c",
        )

    def test_input_terminal_drag_mode_without_keyboard_protocol_support(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_terminal_drag_mode_without_keyboard_protocol_support",
        )

    def test_input_ctrl_v_with_lock_modifiers_does_not_corrupt_input_or_commands(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_ctrl_v_with_lock_modifiers_does_not_corrupt_input_or_commands",
        )

    def test_input_unknown_keyboard_packets_never_become_text_or_escape_focus(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_unknown_keyboard_packets_never_become_text_or_escape_focus",
        )

    def test_input_locked_login_and_profile_controls_preserve_paste_and_focus(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_locked_login_and_profile_controls_preserve_paste_and_focus",
        )

    def test_input_live_drag_across_soft_wrap_and_upward_across_lines(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_live_drag_across_soft_wrap_and_upward_across_lines",
        )

    def test_input_repainting_never_shows_cursor_in_history(self):
        self.exercise(
            test_input_controls.InputControls, "test_repainting_never_shows_cursor_in_history"
        )

    def test_input_selection_remains_visible_without_terminal_colors(self):
        self.exercise(
            test_input_controls.InputControls,
            "test_selection_remains_visible_without_terminal_colors",
        )

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

    def test_message_menu_waits_for_current_position_after_navigation(self):
        self.exercise(
            test_message_interactions.MessageInteractions,
            "test_keyboard_menu_stays_next_to_selected_message_after_scroll_and_resize",
        )

    def test_member_selection_waits_for_unfiltered_rows(self):
        members = importlib.import_module("test_members")
        self.exercise(
            members.Members,
            "test_keyboard_member_menu_opens_beside_selected_member",
        )

    def test_dm_selection_waits_for_both_row_styles(self):
        members = importlib.import_module("test_members")
        self.exercise(
            members.Members,
            "test_dm_arrows_visibly_move_selection_without_changing_role_colors",
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
            self.exercise(terminal_cases.TerminalTests, method)

    def test_media_panels_wait_for_close_in_each_graphics_protocol(self):
        ffmpeg = os.environ.get("PAPO_TEST_FFMPEG") or shutil.which("ffmpeg")
        self.assertTrue(ffmpeg, "FFmpeg is required for terminal media scenarios")
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
    test_main(1, verbosity=2)
