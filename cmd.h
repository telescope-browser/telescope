struct buffer;

struct cmd {
	const char	*cmd;
	void		(*fn)(struct buffer *);
	const char	*descr;
};
extern struct cmd cmds[];

#define CMD(fnname, descr)	void fnname(struct buffer *)
#define DEFALIAS(s, d)		/* nothing */

DEFALIAS(q, cmd_kill_telescope)
DEFALIAS(w, cmd_write_buffer)
DEFALIAS(tabn, cmd_tab_next)
DEFALIAS(tabnew, cmd_tab_new)
DEFALIAS(tabp, cmd_tab_previous)
DEFALIAS(wq, cmd_kill_telescope)
DEFALIAS(open, cmd_load_url)

CMD(cmd_backward_char,		"Move point one character backward.");
CMD(cmd_backward_paragraph,	"Move point on paragraph backward.");
CMD(cmd_beginning_of_buffer,	"Move point to the beginning of the buffer.");
CMD(cmd_bookmark_page,		"Save a page in the bookmark file.");
CMD(cmd_cache_info,		"Show cache stats.");
CMD(cmd_clear_minibuf,		"Clear the echo area.");
CMD(cmd_client_certificate_info,"Show the active client certificate.");
CMD(cmd_dec_fill_column,	"Decrement fill-column by two.");
CMD(cmd_end_of_buffer,		"Move the point to the end of the buffer.");
CMD(cmd_execute_extended_command, "Execute an internal command.");
CMD(cmd_forward_char,		"Move point one character forward.");
CMD(cmd_forward_paragraph,	"Move point one paragraph forward.");
CMD(cmd_home,			"Go to the home directory.");
CMD(cmd_inc_fill_column,	"Increment fill-column by two");
CMD(cmd_insert_current_candidate, "Copy the current selection text as minibuffer input.");
CMD(cmd_kill_telescope,		"Quit Telescope.");
CMD(cmd_link_select,		"Select and visit a link using the minibuffer.");
CMD(cmd_list_bookmarks,		"Load the bookmarks page.");
CMD(cmd_load_current_url,	"Edit the current URL.");
CMD(cmd_load_url,		"Prompt for an URL.");
CMD(cmd_mini_abort,		"Abort the current minibuffer action.");
CMD(cmd_mini_complete_and_exit,	"Complete the current minibuffer action.");
CMD(cmd_mini_delete_backward_char, "Delete the character before the point.");
CMD(cmd_mini_delete_char,	"Delete the character after the point.");
CMD(cmd_mini_goto_beginning,	"Select the first completion.");
CMD(cmd_mini_goto_end,		"Select the last completion.");
CMD(cmd_mini_kill_line,		"Delete from point until the end of the line.");
CMD(cmd_mini_kill_whole_line,	"Delete the whole line.");
CMD(cmd_mini_next_history_element, "Load the next history element.");
CMD(cmd_mini_previous_history_element, "Load the previous history element.");
CMD(cmd_mini_scroll_down,	"Scroll completions up by one visual page");
CMD(cmd_mini_scroll_up,		"Scroll completions up by one visual page");
CMD(cmd_move_beginning_of_line,	"Move point at the beginning of the current visual line.");
CMD(cmd_move_end_of_line,	"Move point at the end of the current visual line.");
CMD(cmd_next_button,		"Move point to the next link.");
CMD(cmd_next_completion,	"Select the next completion.");
CMD(cmd_next_heading,		"Move point to the next heading.");
CMD(cmd_next_line,		"Move point to the next visual line.");
CMD(cmd_next_page,		"Go forward in the page history.");
CMD(cmd_olivetti_mode,		"Toggle olivetti-mode.");
CMD(cmd_other_window,		"Select the other window.");
CMD(cmd_previous_button,	"Move point to the previous link.");
CMD(cmd_previous_completion,	"Select the previous completion.");
CMD(cmd_previous_heading,	"Move point to the previous heading.");
CMD(cmd_previous_line,		"Move point to the previous visual line.");
CMD(cmd_previous_page,		"Go backward in the page history.");
CMD(cmd_push_button,		"Follow link at point or toggle pre-visibility.");
CMD(cmd_push_button_new_tab,	"Follow link at point in a new tab.");
CMD(cmd_redraw,			"Redraw the screen.");
CMD(cmd_reload_page,		"Reload the current page.");
CMD(cmd_reply_last_input,	"Reply last input request.");
CMD(cmd_root,			"Go to the root directory.");
CMD(cmd_scroll_down,		"Scroll down by one visual page");
CMD(cmd_scroll_line_down,	"Scroll down by one line");
CMD(cmd_scroll_line_up,		"Scroll up by one line.");
CMD(cmd_scroll_up,		"Scroll up by one visual page");
CMD(cmd_search,			"Search using the preferred search engine");
CMD(cmd_suspend_telescope,	"Suspend the current Telescope session.");
CMD(cmd_swiper,			"Jump to a line using the minibuffer.");
CMD(cmd_tab_close,		"Close the current tab.");
CMD(cmd_tab_close_other,	"Close all tabs but the current one.");
CMD(cmd_tab_move,		"Move the current tab to the right.");
CMD(cmd_tab_move_to,		"Move the current tab to the left.");
CMD(cmd_tab_new,		"Open a new tab.");
CMD(cmd_tab_next,		"Focus next tab.");
CMD(cmd_tab_previous,		"Focus previous tab.");
CMD(cmd_tab_select,		"Switch to a tab using the minibuffer.");
CMD(cmd_tab_undo_close,		"Reopen last closed tab.");
CMD(cmd_toc,			"Jump to a heading using the minibuffer.");
CMD(cmd_toggle_downloads,	"Toggle the downloads side window.");
CMD(cmd_toggle_help,		"Toggle side window with help.");
CMD(cmd_toggle_pre_wrap,	"Toggle the wrapping of preformatted blocks.");
CMD(cmd_toggle_styling,		"Toggle the page styling.");
CMD(cmd_unload_certificate,	"Forget the certificate on this page.");
CMD(cmd_up,			"Go up one level.");
CMD(cmd_use_certificate,	"Use a certificate for the current page.");
CMD(cmd_write_buffer,		"Save the current page to the disk.");
