CFLAGS=-g3 -O0

# getaddrinfo ntp-wwv.nist.gov time: sin_port: 9472; sin_addr: 132.163.97.5
test: a.out
	./a.out localhost
	./a.out localhost time
	./a.out ntp-wwv.nist.gov time
	@ test `./a.out ntp-wwv.nist.gov time | sed -e 's%^.*sin_addr '%%` == \
	     132.163.97.5 || echo "ERROR:  Wrong addr of ntp-wwv.nist.gov"

gdb: a.out
	gdb --args ./a.out localhost

a.out: gethostbyname_static.c gethostbyname_proxy
	${CC} ${CFLAGS} -DSTANDALONE $< -o $@

gethostbyname_proxy: gethostbyname_proxy.c
	${CC} ${CFLAGS} $< -o $@

clean:
	rm -f a.out *.o gethostbyname_proxy
	rm -f ${MANA_ROOT}/bin/gethostbyname_proxy

dist: clean
	dir=`basename $$PWD` && cd .. && tar zcvf ./$$dir.tar.gz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tar.gz

install:
	make gethostbyname_proxy
	cp -f gethostbyname_proxy ../../../bin/

