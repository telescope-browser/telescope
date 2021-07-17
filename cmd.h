struct buffer;

#define CMD(fnname)	void fnname(struct buffer *)
#define DEFALIAS(s, d)	/* nothing */

CMD(cmd_backward_char);
CMD(cmd_backward_paragraph);
CMD(cmd_beginning_of_buffer);
CMD(cmd_bookmark_page);
CMD(cmd_clear_minibuf);
CMD(cmd_dec_fill_column);
CMD(cmd_end_of_buffer);
CMD(cmd_execute_extended_command);
CMD(cmd_forward_char);
CMD(cmd_forward_paragraph);
CMD(cmd_inc_fill_column);
CMD(cmd_insert_current_candidate);
CMD(cmd_kill_telescope);
CMD(cmd_link_select);
CMD(cmd_list_bookmarks);
CMD(cmd_load_current_url);
CMD(cmd_load_url);
CMD(cmd_mini_abort);
CMD(cmd_mini_complete_and_exit);
CMD(cmd_mini_delete_backward_char);
CMD(cmd_mini_delete_char);
CMD(cmd_mini_goto_beginning);
CMD(cmd_mini_goto_end);
CMD(cmd_mini_kill_line);
CMD(cmd_mini_next_history_element);
CMD(cmd_mini_previous_history_element);
CMD(cmd_move_beginning_of_line);
CMD(cmd_move_end_of_line);
CMD(cmd_next_button);
CMD(cmd_next_completion);
CMD(cmd_next_heading);
CMD(cmd_next_line);
CMD(cmd_next_page);
CMD(cmd_olivetti_mode);
CMD(cmd_previous_button);
CMD(cmd_previous_completion);
CMD(cmd_previous_heading);
CMD(cmd_previous_line);
CMD(cmd_previous_page);
CMD(cmd_push_button);
CMD(cmd_push_button_new_tab);
CMD(cmd_redraw);
CMD(cmd_reload_page);
CMD(cmd_scroll_down);
CMD(cmd_scroll_line_down);
CMD(cmd_scroll_line_up);
CMD(cmd_scroll_up);
CMD(cmd_suspend_telescope);
CMD(cmd_swiper);
CMD(cmd_tab_close);
CMD(cmd_tab_close_other);
CMD(cmd_tab_move);
CMD(cmd_tab_move_to);
CMD(cmd_tab_new);
CMD(cmd_tab_next);
CMD(cmd_tab_previous);
CMD(cmd_tab_select);
CMD(cmd_toc);
CMD(cmd_toggle_help);
CMD(cmd_toggle_pre_wrap);

DEFALIAS(q, cmd_kill_telescope)
DEFALIAS(tabn, cmd_tab_next)
DEFALIAS(tabnew, cmd_tab_new)
DEFALIAS(tabp, cmd_tab_previous)
DEFALIAS(wq, cmd_kill_telescope)
