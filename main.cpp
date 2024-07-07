#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <set>
#include <sstream>
#include <cmath>
#include <unordered_map>
#include <regex>
#include <unordered_set>


struct NGram {
    std::vector<std::string> words;
    int count{};
    double probability{};

    // equality operator
    bool operator==(const NGram &other) const {
        return (words == other.words);
    }
};


enum SmoothingType {
    GOOD_TURING,
    KNESER_NEY
};


void printCorpusContents(const std::string &fileName) {
    std::ifstream corpusFile;
    corpusFile.open(fileName);
    if (!corpusFile.is_open()) {
        std::cerr << "Unable to open the file." << std::endl;
    }

    std::string line;
    while (std::getline(corpusFile, line)) {
        std::cout << line << "\n";
    }
}


std::vector<std::string> countCorpus(const std::string &fileName, bool unique) {
    std::ifstream corpusFile;
    corpusFile.open(fileName);
    if (!corpusFile.is_open()) {
        std::cerr << "Unable to open the file." << std::endl;
        return {};
    }

    std::vector<std::string> words;
    std::string word;
    if (unique) {
        while (corpusFile >> word) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            if (std::find(words.begin(), words.end(), word) == words.end()) {
                words.push_back(word);
            }
        }
    }
    else {
        while (corpusFile >> word) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            words.push_back(word);
        }
    }

    return words;
}


std::vector<std::string> preprocessAndTokenize(const std::string &fileName, bool xml) {
    std::ifstream corpusFile;
    corpusFile.open(fileName);
    if (!corpusFile.is_open()) {
        std::cerr << "Unable to open the file." << std::endl;
        return {};
    }

    // processing XML file
    if (xml) {
        // read the XML content
        std::string xmlContent((std::istreambuf_iterator<char>(corpusFile)), std::istreambuf_iterator<char>());

        // remove XML tags
        std::regex xmlTagRegex("<[^>]+>");
        std::string plainText = std::regex_replace(xmlContent, xmlTagRegex, "");
        std::string cleanedText = std::regex_replace(plainText, std::regex("<p>|</?p>|<div>|</?div>|<body>|</?body>"), "");

        std::vector<std::string> tokens;
        std::string line;
        std::vector<std::string> lines;
        std::istringstream iss(cleanedText);
        while (std::getline(iss, line)) {
            if (line != "" && line != " ") {
                lines.push_back(line);
            }
        }

        for (const auto &line: lines) {
            // input string stream made from current file line
            std::istringstream iss(line);
            // storing each token
            std::string token;

            // add opening tag
            tokens.emplace_back("<s>");

            while (iss >> token) {
                // remove punctuations (.,:;!?) from token
                token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) { return std::ispunct(c); }),
                            token.end());
                // convert token to lower case
                std::transform(token.begin(), token.end(), token.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                // add token to list of tokens
                tokens.push_back(token);
            }

            // add closing tag
            tokens.emplace_back("</s>");
        }

        corpusFile.close();
        return tokens;
    }

    // processing TEXT file
    else {
        std::string line;
        std::vector<std::string> tokens;

        while (std::getline(corpusFile, line)) {
            // input string stream made from current file line
            std::istringstream iss(line);
            // storing each token
            std::string token;

            // add opening tag
            tokens.emplace_back("<s>");

            while (iss >> token) {
                // remove punctuations (.,:;!?) from token
                token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) { return std::ispunct(c); }),
                            token.end());
                // convert token to lower case
                std::transform(token.begin(), token.end(), token.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                // add token to list of tokens
                tokens.push_back(token);
            }

            // add closing tag
            tokens.emplace_back("</s>");
        }

        corpusFile.close();
        return tokens;
    }
}


template<typename NGramType>
std::vector<NGramType> buildNGrams(const std::vector<std::string> &tokens, int n, SmoothingType smoothingType) {
    std::vector<NGramType> ngrams;
    if (n < 2) {
        std::cerr << "N-grams must have a minimum size of 2." << std::endl;
        return ngrams;
    }

    std::unordered_map<std::string, int> wordCount;
    std::unordered_map<std::string, int> precedingWordCount;

    // preallocate memory for vector of N-grams
    ngrams.reserve(tokens.size() - (n - 1));

    for (size_t i = 0; i < tokens.size() - (n - 1); ++i) {
        NGramType ngram;
        for (int j = 0; j < n; ++j) {
            ngram.words.push_back(tokens[i+j]);
        }
        ngram.count = 1;

        // count occurrences of every word
        std::string nGramKey;
        // preallocate memory to avoid frequent reallocations during string concatenation inside the loop (20 - arbitrary average word length estimation)
        nGramKey.reserve(n * 20);
        for (const auto &word : ngram.words) {
            nGramKey += word + " ";
        }
        wordCount[nGramKey]++;
        precedingWordCount[nGramKey.substr(0, ngram.words[0].size() + 1)]++;

        // check for duplicate N-grams
        auto it = std::find(ngrams.begin(), ngrams.end(), ngram);
        if (it != ngrams.end()) {
            it->count++;
        } else {
            ngrams.push_back(ngram);
        }
    }

    // calculating N-gram probabilities
    //
    // probability for 2-grams meaning --> likelihood of encountering the 2. word given the 1. word
    // --> (the, cat) having probability of 0.1 means that given the occurrence of "the", there is a 10% chance that the next word will be "cat"
    //
    // probability for 3-grams meaning --> likelihood of encountering the last word given the first n-1 words

    // Good Turing smoothing
    if (smoothingType == GOOD_TURING) {
        std::unordered_map<std::string, int> Nc;
        for (auto& ngram : ngrams) {
            int matches = 0;
            std::string precedingWords;
            precedingWords.reserve(n-1);    // pre-allocation
            for (int i = 0; i < n-1; ++i) {
                precedingWords += ngram.words[i] + " ";
            }
            Nc[precedingWords]++;
        }

        std::unordered_map<int, int> eachOccurrences;
        for (auto it = Nc.begin(); it != Nc.end(); ++it) {
            if (eachOccurrences.find(it->second) != eachOccurrences.end()) {
                eachOccurrences[it->second]++;
            }
            else {
                eachOccurrences[it->second] = 1;
            }
        }

        for (auto& ngram : ngrams) {
            std::string precedingWords;
            for (int i = 0; i < n-1; ++i) {
                precedingWords += ngram.words[i] + " ";
            }
            double n = Nc[precedingWords];
            double c = ngram.count;

            double c1 = c + 1;
            double c1_occurrences = eachOccurrences[c+1];
            double c_occurrences = eachOccurrences[c];

            if (eachOccurrences.find(c)->second != 0 && eachOccurrences.find(c1)->second != 0) {
                double c_asterisk = c1 * c1_occurrences / c_occurrences;
                ngram.probability = static_cast<double>(c_asterisk) / n;
            }
            else {
                ngram.probability = static_cast<double>(eachOccurrences[1]) / n;
            }
        }
    }
    // Kneser-Ney smoothing
    else if (smoothingType == KNESER_NEY) {
        std::unordered_map<std::string, int> Nc;
        for (auto& ngram : ngrams) {
            int matches = 0;
            std::string precedingWords;
            precedingWords.reserve(n-1);    // pre-allocation
            for (int i = 0; i < n-1; ++i) {
                precedingWords += ngram.words[i] + " ";
            }
            Nc[precedingWords]++;
        }

        std::unordered_map<int, int> eachOccurrences;
        for (auto it = Nc.begin(); it != Nc.end(); ++it) {
            if (eachOccurrences.find(it->second) != eachOccurrences.end()) {
                eachOccurrences[it->second]++;
            }
            else {
                eachOccurrences[it->second] = 1;
            }
        }

        std::unordered_set<std::string> uniqueTokens(tokens.begin(), tokens.end());
        double numUniqueWords = uniqueTokens.size();

        for (auto& ngram : ngrams) {
            std::string precedingWords;
            for (int i = 0; i < n - 1; ++i) {
                precedingWords += ngram.words[i] + " ";
            }
            std::string currentWord = ngram.words[n-1];

            // discounting parameter
            const double D = 0.5;

            // unique occurrences
            double n = Nc[precedingWords];
            // all occurrences
            double c = Nc[precedingWords] + (ngram.count - 1);

            // normalization constant
            const double lambda = D * n / c;

            // continuation probability
            double continuationProbability = n / numUniqueWords;

            ngram.probability = (std::max(ngram.count - D, 0.0) / c) + (lambda * continuationProbability);
        }
    }

    return ngrams;
}


void printNGrams(const std::vector<NGram>& ngrams) {
    for (const auto &ngram: ngrams) {
        std::cout << "(";
        for (const auto& word : ngram.words) {
            std::cout << word << ", ";
        }
        std::cout << "\b\b): " << ngram.count << " (" << ngram.probability << ")\n";
    }
}


void saveModelToFile(const std::vector<NGram>& ngrams, const std::string& fileName) {
    std::ofstream outFile(fileName);
    if (!outFile.is_open()) {
        std::cerr << "Unable to open the file for writing." << std::endl;
        return;
    }

    for (const auto &ngram : ngrams) {
        for (const auto &word : ngram.words) {
            outFile << word << " ";
        }
        outFile << ngram.count << " " << ngram.probability << "\n";
    }

    outFile.close();
}


std::vector<NGram> readModel(const std::string &fileName, int n) {
    // open file
    std::vector<NGram> ngrams;
    std::ifstream inFile(fileName);
    if (!inFile.is_open()) {
        std::cerr << "Unable to open the file for writing." << std::endl;
        return ngrams;
    }

    std::string line;
    while(std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string token;
        NGram ngram;

        int words = 0;
        // read words
        while (iss >> token && words < n) {
            ngram.words.push_back(token);
            words++;
        }

        // read count
        ngram.count = std::stoi(token);


        // read probability
        iss >> token;
        ngram.probability = std::stod(token);

        ngrams.push_back(ngram);
    }

    return ngrams;
}


std::vector<NGram> createTestNgrams(std::vector<NGram>& model, std::vector<std::string>& testTokens, int n) {
    std::vector<NGram> testNgrams;
    for (size_t i = 0; i < testTokens.size() - (n - 1); ++i) {
        NGram ngram;
        for (int j = 0; j < n; ++j) {
            ngram.words.push_back(testTokens[i + j]);
        }
        ngram.count = 0;
        ngram.probability = 0.0;
        testNgrams.push_back(ngram);
    }

    // assign matching probabilities from already-built 2-gram model
    for (auto &testNgram : testNgrams) {
        for (const auto &ngram: model) {
            int matches = 0;
            for (int i = 0; i < n; ++i) {
                if (testNgram.words[i] == ngram.words[i]) {
                    matches++;
                }
            }
            // 2-gram found in model
            if (matches == n) {
                testNgram.probability = ngram.probability;
                break;
            }
            else {
                testNgram.probability = static_cast<double>(1) / static_cast<double>(model.size());
            }
        }
    }

    return testNgrams;
}


double calculateSentenceProbability(std::vector<NGram>& testNgrams) {
    double probability = 1.0;
    for (const auto& ngram : testNgrams) {
        probability *= ngram.probability;
    }
    return probability;
}


double calculateModelPerplexity(const std::vector<NGram> &model) {
    double perplexity = 1.0;
    for (const auto &ngram: model) {
        double buffer = std::pow(ngram.probability, -1 / static_cast<double>(model.size()));
        perplexity *= buffer;
    }
    return perplexity;
}


double calculatePerplexity(const std::vector<NGram> &model, std::vector<std::string>& testTokens, int n) {
    // acquire words for N-grams from vector of test tokens
    std::vector<double> probabilities;
    bool matchFound = false;
    for (size_t i = 0; i < testTokens.size() - (n - 1); ++i) {
        std::vector<std::string> words;
        for (size_t j = 0; j < n; ++j) {
            words.push_back(testTokens[i + j]);
        }

        // apply N-gram model to test tokens (words)
        for (const auto &ngram : model) {
            int matches = 0;
            // find matches inside current N-gram
            for (size_t k = 0; k < words.size(); ++k) {
                if (ngram.words[k] == words[k]) {
                    matches++;
                }
            }
            // if match found
            if (matches == n) {
                probabilities.push_back(ngram.probability);
                matchFound = true;
                break;
            }
            // Good Turing for zero frequency tokens (not yet seen tokens) --> c* / N
            if (!matchFound) {
                const unsigned int N = model.size();
                const double c_asterisk = 1.0;
                probabilities.push_back(c_asterisk/N);
                break;
            }
        }
    }

    double perplexity = 1.0;
    for (const auto &p : probabilities) {
        double buffer = std::pow(p, -1 / static_cast<double>(testTokens.size()));
        perplexity *= buffer;
    }
    return perplexity;
}


void ngramMenu() {
    std::cout << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "What kind of language model would you like to build?" << std::endl;
    std::cout << "2 ... 2-GRAM" << std::endl;
    std::cout << "3 ... 3-GRAM" << std::endl;
    std::cout << "4 ... EXIT" << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "Your choice: ";
}


void smoothingMenu() {
    std::cout << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "What kind of smoothing would you like to choose?" << std::endl;
    std::cout << "1 ... GOOD TURING" << std::endl;
    std::cout << "2 ... KNESER-NEY" << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "Your choice: ";
}



int main() {
    const std::string ABS_PATH = R"(G:\Other computers\Home Desktop\Google Drive\FERI\MAGISTERIJ\2. semester\Jezikovne Tehnologije\vaje\vaja2\vaja2\korpus\)";

    // train corpus
    std::string corpusName = "kas-5000.text.txt";
    std::string trainFileName = ABS_PATH + corpusName;

    // test corpus
    std::string testFileName = ABS_PATH + "kas-4000.text.txt";

    size_t dotPos = corpusName.find('.');
    std::string corpusNameShort;

    /*printCorpusContents(fileName);

    std::vector<std::string> corpusWords = countCorpus(fileName, false);
    std::vector<std::string> vocabulary = countCorpus(fileName, true);

    std::cout << "number of all words in corpus: " << corpusWords.size() << std::endl;
    std::cout << "number of words in vocabulary (unique corpus words): " << vocabulary.size() << std::endl;*/

    // preprocess train corpus
    std::vector<std::string> trainTokens = preprocessAndTokenize(trainFileName, false);

    bool buildModel = false;

    bool running = true;
    int ngramSelection;

    while (running) {
        ngramMenu();
        std::cin >> ngramSelection;
        // build 2-gram model
        if (ngramSelection == 2) {
            int smoothingSelection;
            SmoothingType smoothingType;
            smoothingMenu();
            std::cin >> smoothingSelection;
            switch (smoothingSelection) {
                case 1:
                    smoothingType = GOOD_TURING;
                    corpusNameShort = corpusName.substr(0, dotPos) + "-good-turing-bigrams.txt";
                    std::cout << "building 2-gram model with Good Turing smoothing..." << std::endl;
                    break;
                case 2:
                    smoothingType = KNESER_NEY;
                    std::cout << "building 2-gram model with Kneser-Ney smoothing..." << std::endl;
                    corpusNameShort = corpusName.substr(0, dotPos) + "-kneser-ney-bigrams.txt";
                    break;
            }

            if (buildModel) {
                std::vector<NGram> model = buildNGrams<NGram>(trainTokens, 2, smoothingType);
                saveModelToFile(model, corpusNameShort);
            }

            // load model from file
            std::vector<NGram> loadedModel = readModel(corpusNameShort, 2);
            //printNGrams(loadedModel);

            // test model on test corpus
            std::vector<std::string> testTokens = preprocessAndTokenize(testFileName, false);
            std::vector<NGram> testNgrams = createTestNgrams(loadedModel, testTokens, 2);
            double probability = calculateSentenceProbability(testNgrams);
            std::cout << std::endl << "probability of sentence: " << probability << std::endl;
            double perplexity = calculatePerplexity(loadedModel, testTokens, 2);
            //double modelPerplexity = calculateModelPerplexity(loadedModel);
            std::cout << std::endl << "perplexity of 2-gram model: " << perplexity << std::endl;
        }
        else if (ngramSelection == 3) {
            int smoothingSelection;
            SmoothingType smoothingType;
            smoothingMenu();
            std::cin >> smoothingSelection;
            switch (smoothingSelection) {
                case 1:
                    smoothingType = GOOD_TURING;
                    corpusNameShort = corpusName.substr(0, dotPos) + "-good-turing-trigrams.txt";
                    std::cout << "building 3-gram model with Good Turing smoothing..." << std::endl;
                    break;
                case 2:
                    smoothingType = KNESER_NEY;
                    std::cout << "building 3-gram model with Kneser-Ney smoothing..." << std::endl;
                    corpusNameShort = corpusName.substr(0, dotPos) + "-kneser-ney-trigrams.txt";
                    break;
            }

            if (buildModel) {
                std::vector<NGram> model = buildNGrams<NGram>(trainTokens, 3, smoothingType);
                saveModelToFile(model, corpusNameShort);
            }

            // load model from file
            std::vector<NGram> loadedModel = readModel(corpusNameShort, 3);
            //printNGrams(loadedModel);

            // test model on test corpus
            std::vector<std::string> testTokens = preprocessAndTokenize(testFileName, false);
            std::vector<NGram> testNgrams = createTestNgrams(loadedModel, testTokens, 3);
            double probability = calculateSentenceProbability(testNgrams);
            std::cout << std::endl << "probability of sentence: " << probability << std::endl;
            double perplexity = calculatePerplexity(loadedModel, testTokens, 3);
            //double modelPerplexity = calculateModelPerplexity(loadedModel);
            std::cout << std::endl << "perplexity of 3-gram model: " << perplexity << std::endl;
        }
        else if (ngramSelection == 4) {
            running = false;
        }
    }

    return 0;
}



//
// RESULTS FOR PROBABILITIES OF TEST CORPUS SENTENCES
//
// GOOD TURING SMOOTHING
//
// 2-GRAMS:
//
// DIPLOMSKO DELO (shorter exact match) --> 8.81075e-05
// Za pomoč pri izdelavi diplomske naloge. (longer exact match) --> 3.83895e-11
// Za pomoč pri izdelavi magistrske naloge. (almost match) --> 2.2826e-19
// Lorem ipsum dolor sit amet, consectetur adipiscing elit. (unseen) --> 2.0708e-37
//
// 3-GRAMS:
//
// DIPLOMSKO DELO (shorter exact match) --> 0.00455046
// Za pomoč pri izdelavi diplomske naloge. (longer exact match) --> 6.35111e-11
// Za pomoč pri izdelavi magistrske naloge. (almost match) --> 2.27989e-17
// Lorem ipsum dolor sit amet, consectetur adipiscing elit. (unseen) --> 5.76711e-34
//
//
// KNESER-NEY SMOOTHING
//
// 2-GRAMS:
//
// DIPLOMSKO DELO (shorter exact match) --> 0.000137583
// Za pomoč pri izdelavi diplomske naloge. (longer exact match) --> 2.63829e-11
// Za pomoč pri izdelavi magistrske naloge. (almost match) --> 1.87804e-18
// Lorem ipsum dolor sit amet, consectetur adipiscing elit. (unseen) --> 2.0708e-37
//
// 3-GRAMS:
//
// DIPLOMSKO DELO (shorter exact match) --> 0.000164908
// Za pomoč pri izdelavi diplomske naloge. (longer exact match) --> 8.34643e-14
// Za pomoč pri izdelavi magistrske naloge. (almost match) --> 8.34393e-20
// Lorem ipsum dolor sit amet, consectetur adipiscing elit. (unseen) --> 5.76711e-34
//