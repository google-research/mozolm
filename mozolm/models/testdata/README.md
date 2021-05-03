## Test data in this directory

### Character N-gram FSTs

Constructed using [OpenGrm N-Gram](http://www.openfst.org/twiki/bin/view/GRM/NGramLibrary)
toolkit from a small subset of books from [Project Gutenberg](https://www.gutenberg.org/).

#### English

The dataset consists of 61,119,988 characters.

1.  `gutenberg_en_char_ngram.tsv`: English symbol table corresponding to Dasher
    keyboard for English with extended punctuation.

1.  `gutenberg_en_char_ngram_o4_wb.fst`: unpruned 4-gram character-based FST
    with Witten-Bell smoothing and no pruning. The model is stored in
    [OpenFst](http://www.openfst.org) binary format.
