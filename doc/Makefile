.SUFFIXES: .txt .html


.txt.html:
	asciidoctor -b html5 -a toc=left -a stylesheet=clean.css -o $@ $<

HTML = $(patsubst %.txt,%.html,$(wildcard *.txt))

DOXINDEX = refdoc/html/index.html
all: $(HTML) $(DOXINDEX)

$(DOXINDEX) : ../inc/upnp.h
	doxygen Doxyfile

clean:
	rm -f $(HTML)
