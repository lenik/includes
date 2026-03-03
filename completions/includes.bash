# Bash completion for includes. Source from ~/.bashrc or install to
# /etc/bash_completion.d/ (or use: source /path/to/includes.bash)
_includes() {
	local cur prev
	cur="${COMP_WORDS[COMP_CWORD]}"
	prev="${COMP_WORDS[COMP_CWORD-1]:-}"

	case "$prev" in
		-I)
			compopt -o dirnames 2>/dev/null || true
			COMPREPLY=($(compgen -d -S / -- "$cur"))
			return 0
			;;
		-T)
			COMPREPLY=($(compgen -W "gcc msc" -- "$cur"))
			return 0
			;;
		-t|-L)
			COMPREPLY=()
			return 0
			;;
	esac
	if [[ "$cur" == -* ]]; then
		COMPREPLY=($(compgen -W "-I -T -t -L -d -u -a -A -p -c -e -n -m -s -j -g -P -q -v --help --echo --exists --missing" -- "$cur"))
		return 0
	fi
	compopt -o default 2>/dev/null || true
	COMPREPLY=()
}
complete -F _includes includes 2>/dev/null || true
