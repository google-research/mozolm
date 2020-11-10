# MozoLM

Data compiled from a subset of English Wikipedia.

en_wiki_1Mline_char_bigram.matrix.txt is a dense symmetric matrix of bigram
character counts.

en_wiki_1Mline_char_bigram.rows.txt provides the unicode codepoints associated
with each row/column in the dense count matrix.  Index 0 is reserved for
begin-of-string and end-of-string.
