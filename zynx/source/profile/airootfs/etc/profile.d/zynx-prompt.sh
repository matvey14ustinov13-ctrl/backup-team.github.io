# Zynx Linux shell defaults.
if [[ $- == *i* ]]; then
    if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
        PS1='\[\e[33m\]\u@\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]# '
    else
        PS1='\[\e[33m\]\u@\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
    fi
fi

HISTCONTROL=ignoreboth
HISTSIZE=10000
HISTFILESIZE=20000
shopt -s histappend checkwinsize
