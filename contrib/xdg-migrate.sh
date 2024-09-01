#!/bin/sh

old_path="$HOME/.telescope"

Die() {
	printf 'error: %s\n' "$1" 1>&2
	exit 1
}

[ -e "$old_path" ] || Die "$old_path does not exist."
[ -d "$old_path" ] || Die "$old_path is not a directory."

xdg_config="${XDG_CONFIG_HOME:-$HOME/.config}/telescope"
xdg_data="${XDG_DATA_HOME:-$HOME/.local/share}/telescope"
xdg_cache="${XDG_CACHE_HOME:-$HOME/.cache}/telescope"

mkdir -p "$xdg_config" "$xdg_data" "$xdg_cache"

for filepath in \
	"$xdg_config/config" \
	"$xdg_data/pages" \
	"$xdg_data/bookmarks.gmi" \
	"$xdg_data/known_hosts"
do
	old_file="$old_path/${filepath##*/}"
	[ -e "$old_file" ] && cp -R "$old_file" "filepath"
done

printf "\
WARNING: the old ~/.telescope directory will be removed.

Every file/directory other than the following has not been copied:
    - config
    - bookmarks.gmi
    - known_hosts
    - pages/

Are you sure? [Y/n] "

read -r reply
case $reply in
	[yY]) rm -r "$old_path" && printf 'done\n' ;;
esac
