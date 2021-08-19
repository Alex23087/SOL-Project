alias server='make CFLAGS=-DDEBUG server && ./server' 
alias client='make CFLAGS=-DDEBUG client && ./client -f /tmp/LSOfilestorage.sk -W /mnt/e/Progetti/SOL-Project/config.txt'
alias killserver='kill -SIGKILL $(ps -a | grep server | grep -oE "[0-9]{4,5}")'
alias clocall='cloc *.c *.h'
