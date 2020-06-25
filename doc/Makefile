.SUFFIXES: .txt .html


.txt.html:
	asciidoctor -b html5 -a toc=left -a stylesheet=clean.css -o $@ $<

HTML = $(patsubst %.txt,%.html,$(wildcard *.txt))

all: $(HTML) Dox

Dox:
	doxygen Doxyfile

clean:
	rm -f $(HTML)
