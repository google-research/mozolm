## Test data in this directory

### N-gram FSTs

Constructed using [OpenGrm N-Gram](http://www.openfst.org/twiki/bin/view/GRM/NGramLibrary)
toolkit from a small subset of books from [Project Gutenberg](https://www.gutenberg.org/), or from [English Wikipedia](https://en.wikipedia.org/wiki/Main_Page) samples in mozolm/data.

#### Gutenberg data and models

The dataset consists of 61,119,988 characters.

1.  `gutenberg_en_char_ngram.tsv`: English symbol table corresponding to Dasher
    keyboard for English with extended punctuation.

1.  `gutenberg_en_char_ngram_o4_wb.fst`: unpruned 4-gram character-based FST
    with Witten-Bell smoothing and no pruning. The model is stored in
    [OpenFst](http://www.openfst.org) binary format.

1.  `gutenberg_en_char_ngram_o2_kn.fst`: unpruned bigram character-based FST
    with Kneser-Ney smoothing and no pruning.

1.  `gutenberg_praise_of_folly.txt`: Portion of The Project Gutenberg
    [EBook](https://www.gutenberg.org/cache/epub/9371/pg9371.txt) of The Praise
    of Folly, by Desiderius Erasmus.

#### Wikipedia data and models

1. `en_wiki_1Kline_sample.txt` is a text sample of 1000 lines of sentence
   segmented Wikipedia text, one sentence per line.

1. `en_wiki_100line_dev_sample.txt` is a text sample of 100 lines of sentence
   segmented Wikipedia text one sentence per line, disjoint from
   en_wiki_1Kline_sample.txt.

1. `en_wiki_1Mline_char_bigram.matrix.txt` is a dense symmetric matrix of bigram
   character counts, derived from 1 million lines of sentence segmented
   Wikipedia text.

1. `en_wiki_1Mline_char_bigram.rows.txt` provides the unicode codepoints
   associated with each row/column in the dense count matrix. Index 0 is
   reserved for begin-of-string and end-of-string.

1. `en_wiki_1Kline_sample.katz_word3g.fst`: unpruned 3-gram word-based n-gram
   model with Katz smoothing and no pruning, trained on
   `en_wiki_1Kline_sample.txt`. The model is stored in
   [OpenFst](http://www.openfst.org) binary format.
