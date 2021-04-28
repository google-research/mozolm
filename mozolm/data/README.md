# Sample MozoLM Data

Data compiled from a subset of English Wikipedia.

`en_wiki_1Kline_sample.txt` is a text sample of 1000 lines of sentence segmented
Wikipedia text, one sentence per line.

`en_wiki_100line_dev_sample.txt` is a text sample of 100 lines of sentence
segmented Wikipedia text one sentence per line, disjoint from
en_wiki_1Kline_sample.txt.

`en_wiki_1Mline_char_bigram.matrix.txt` is a dense symmetric matrix of bigram
character counts, derived from 1 million lines of sentence segmented Wikipedia
text.

`en_wiki_1Mline_char_bigram.rows.txt` provides the unicode codepoints associated
with each row/column in the dense count matrix. Index 0 is reserved for
begin-of-string and end-of-string.
