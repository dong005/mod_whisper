# Makefile for mod_whisper

APXS=apxs
APACHECTL=apachectl

all: mod_whisper.la

mod_whisper.la: mod_whisper.c mod_whisper.h
	$(APXS) -c mod_whisper.c

install: mod_whisper.la
	$(APXS) -i -a mod_whisper.la

clean:
	rm -f mod_whisper.o mod_whisper.la mod_whisper.slo mod_whisper.lo
	rm -rf .libs

restart: install
	$(APACHECTL) restart 