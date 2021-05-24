## Test data in this directory

### N-gram FSTs

Constructed using [OpenGrm N-Gram](http://www.openfst.org/twiki/bin/view/GRM/NGramLibrary)
toolkit from a small subset of books from [Project Gutenberg](https://www.gutenberg.org/), or from English Wikipedia samples in mozolm/data.

#### English

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

1. `en_wiki_1Kline_sample.katz_word3g.fst`: unpruned 3-gram word-based FST with
        Katz smoothing and no pruning.  Trained on Wikipedia sample. The model is stored in
        [OpenFst](http://www.openfst.org) binary format.
