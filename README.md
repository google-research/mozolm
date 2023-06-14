[![GitHub license](https://img.shields.io/badge/license-Apache2-blue.svg)](https://github.com/google-research/mozolm/blob/main/LICENSE)
[![C++ version](https://img.shields.io/badge/C++17-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Build Tests (Linux)](https://github.com/google-research/mozolm/workflows/linux/badge.svg)](https://github.com/google-research/mozolm/actions?query=workflow%3A%22linux%22)
[![Build Tests (macOS)](https://github.com/google-research/mozolm/workflows/macos/badge.svg)](https://github.com/google-research/mozolm/actions?query=workflow%3A%22macos%22)
[![Build Tests (Windows)](https://github.com/google-research/mozolm/workflows/windows/badge.svg)](https://github.com/google-research/mozolm/actions?query=workflow%3A%22windows%22)
[![Build Tests (Android)](https://github.com/google-research/mozolm/workflows/android/badge.svg)](https://github.com/google-research/mozolm/actions?query=workflow%3A%22android%22)
[![Build Tests (iOS)](https://github.com/google-research/mozolm/workflows/ios/badge.svg)](https://github.com/google-research/mozolm/actions?query=workflow%3A%22ios%22)

# MozoLM

[<img src="https://img.shields.io/badge/slack-@openaac-yellow.svg?logo=slack">](https://openaac.slack.com/)

A language model serving library, with middleware functionality including mixing
of probabilities from disparate base language model types and tokenizations
along with RPC client/server interactions.

## License

MozoLM is licensed under the terms of the Apache license. See [LICENSE](LICENSE)
for more information.

## Citation

If you use this software in a publication, please cite the accompanying
[paper](https://aclanthology.org/2022.slpat-1.1.pdf) from
[SLPAT 2022](http://www.slpat.org/slpat2022/):

```bibtex
@inproceedings{roark-gutkin-2022-design,
    title = "Design principles of an open-source language modeling microservice package for {AAC} text-entry applications",
    author = "Roark, Brian and Gutkin, Alexander",
    booktitle = "Ninth Workshop on Speech and Language Processing for Assistive Technologies (SLPAT-2022)",
    month = may,
    year = "2022",
    address = "Dublin, Ireland",
    publisher = "Association for Computational Linguistics",
    url = "https://aclanthology.org/2022.slpat-1.1",
    doi = "10.18653/v1/2022.slpat-1.1",
    pages = "1--16",
}
```

## Mandatory Disclaimer

This is not an officially supported Google product.
