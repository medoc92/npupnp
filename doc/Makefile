.SUFFIXES: .txt .html


.txt.html:
	asciidoctor -b html5 -a toc=left -a stylesheet=clean.css -o $@ $<

HTML = $(patsubst %.txt,%.html,$(wildcard *.txt))

all: $(HTML)

clean:
	rm -f $(HTML)
