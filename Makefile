CC= g++-mp-4.7
CFLAGS =  -std=c++11 -g -Wall #-pedantic-errors #-Werror
LIBS = -lz #-L. -lgzstream
FPATH = nz2test

run_query: query
	./query $(FPATH)/lexicon.gz $(FPATH)/i2list $(FPATH)/i2chunk ./urltable/
query: query.cpp
	$(CC) $(CFLAGS) $(LIBS) -o query $^

clean:
	rm -rf query *.dSYM