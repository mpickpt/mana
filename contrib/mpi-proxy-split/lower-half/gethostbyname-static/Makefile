CFLAGS=-g3 -O0

test: a.out
	./a.out localhost

gdb: a.out
	gdb --args ./a.out localhost

a.out: gethostbyname_r.c gethostbyname_r_aux
	${CC} ${CFLAGS} -DSTANDALONE $< -o $@

gethostbyname_r_aux: gethostbyname_r_aux.c
	${CC} ${CFLAGS} $< -o $@

clean:
	rm -f a.out *.o gethostbyname_r_aux

dist: clean
	dir=`basename $$PWD` && cd .. && tar zcvf ./$$dir.tar.gz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tar.gz
