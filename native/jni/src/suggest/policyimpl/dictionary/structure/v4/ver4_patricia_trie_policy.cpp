/*
 * Copyright (C) 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "suggest/policyimpl/dictionary/structure/v4/ver4_patricia_trie_policy.h"

#include <vector>

#include "suggest/core/dicnode/dic_node.h"
#include "suggest/core/dicnode/dic_node_vector.h"
#include "suggest/core/dictionary/unigram_property.h"
#include "suggest/policyimpl/dictionary/structure/pt_common/dynamic_pt_reading_helper.h"
#include "suggest/policyimpl/dictionary/structure/v4/ver4_patricia_trie_node_reader.h"
#include "suggest/policyimpl/dictionary/utils/forgetting_curve_utils.h"
#include "suggest/policyimpl/dictionary/utils/probability_utils.h"

namespace latinime {

// Note that there are corresponding definitions in Java side in BinaryDictionaryTests and
// BinaryDictionaryDecayingTests.
const char *const Ver4PatriciaTriePolicy::UNIGRAM_COUNT_QUERY = "UNIGRAM_COUNT";
const char *const Ver4PatriciaTriePolicy::BIGRAM_COUNT_QUERY = "BIGRAM_COUNT";
const char *const Ver4PatriciaTriePolicy::MAX_UNIGRAM_COUNT_QUERY = "MAX_UNIGRAM_COUNT";
const char *const Ver4PatriciaTriePolicy::MAX_BIGRAM_COUNT_QUERY = "MAX_BIGRAM_COUNT";
const int Ver4PatriciaTriePolicy::MARGIN_TO_REFUSE_DYNAMIC_OPERATIONS = 1024;
const int Ver4PatriciaTriePolicy::MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS =
        Ver4DictConstants::MAX_DICTIONARY_SIZE - MARGIN_TO_REFUSE_DYNAMIC_OPERATIONS;

void Ver4PatriciaTriePolicy::createAndGetAllChildDicNodes(const DicNode *const dicNode,
        DicNodeVector *const childDicNodes) const {
    if (!dicNode->hasChildren()) {
        return;
    }
    DynamicPtReadingHelper readingHelper(mDictBuffer, &mNodeReader);
    readingHelper.initWithPtNodeArrayPos(dicNode->getChildrenPtNodeArrayPos());
    while (!readingHelper.isEnd()) {
        const PtNodeParams ptNodeParams = readingHelper.getPtNodeParams();
        if (!ptNodeParams.isValid()) {
            break;
        }
        bool isTerminal = ptNodeParams.isTerminal() && !ptNodeParams.isDeleted();
        if (isTerminal && mHeaderPolicy->isDecayingDict()) {
            // A DecayingDict may have a terminal PtNode that has a terminal DicNode whose
            // probability is NOT_A_PROBABILITY. In such case, we don't want to treat it as a
            // valid terminal DicNode.
            isTerminal = ptNodeParams.getProbability() != NOT_A_PROBABILITY;
        }
        childDicNodes->pushLeavingChild(dicNode, ptNodeParams.getHeadPos(),
                ptNodeParams.getChildrenPos(), ptNodeParams.getProbability(), isTerminal,
                ptNodeParams.hasChildren(),
                ptNodeParams.isBlacklisted()
                        || ptNodeParams.isNotAWord() /* isBlacklistedOrNotAWord */,
                ptNodeParams.getCodePointCount(), ptNodeParams.getCodePoints());
        readingHelper.readNextSiblingNode(ptNodeParams);
    }
}

int Ver4PatriciaTriePolicy::getCodePointsAndProbabilityAndReturnCodePointCount(
        const int ptNodePos, const int maxCodePointCount, int *const outCodePoints,
        int *const outUnigramProbability) const {
    DynamicPtReadingHelper readingHelper(mDictBuffer, &mNodeReader);
    readingHelper.initWithPtNodePos(ptNodePos);
    return readingHelper.getCodePointsAndProbabilityAndReturnCodePointCount(
            maxCodePointCount, outCodePoints, outUnigramProbability);
}

int Ver4PatriciaTriePolicy::getTerminalPtNodePositionOfWord(const int *const inWord,
        const int length, const bool forceLowerCaseSearch) const {
    DynamicPtReadingHelper readingHelper(mDictBuffer, &mNodeReader);
    readingHelper.initWithPtNodeArrayPos(getRootPosition());
    return readingHelper.getTerminalPtNodePositionOfWord(inWord, length, forceLowerCaseSearch);
}

int Ver4PatriciaTriePolicy::getProbability(const int unigramProbability,
        const int bigramProbability) const {
    if (mHeaderPolicy->isDecayingDict()) {
        // Both probabilities are encoded. Decode them and get probability.
        return ForgettingCurveUtils::getProbability(unigramProbability, bigramProbability);
    } else {
        if (unigramProbability == NOT_A_PROBABILITY) {
            return NOT_A_PROBABILITY;
        } else if (bigramProbability == NOT_A_PROBABILITY) {
            return ProbabilityUtils::backoff(unigramProbability);
        } else {
            // bigramProbability is a bigram probability delta.
            return ProbabilityUtils::computeProbabilityForBigram(unigramProbability,
                    bigramProbability);
        }
    }
}

int Ver4PatriciaTriePolicy::getUnigramProbabilityOfPtNode(const int ptNodePos) const {
    if (ptNodePos == NOT_A_DICT_POS) {
        return NOT_A_PROBABILITY;
    }
    const PtNodeParams ptNodeParams(mNodeReader.fetchNodeInfoInBufferFromPtNodePos(ptNodePos));
    if (ptNodeParams.isDeleted() || ptNodeParams.isBlacklisted() || ptNodeParams.isNotAWord()) {
        return NOT_A_PROBABILITY;
    }
    return getProbability(ptNodeParams.getProbability(), NOT_A_PROBABILITY);
}

int Ver4PatriciaTriePolicy::getShortcutPositionOfPtNode(const int ptNodePos) const {
    if (ptNodePos == NOT_A_DICT_POS) {
        return NOT_A_DICT_POS;
    }
    const PtNodeParams ptNodeParams(mNodeReader.fetchNodeInfoInBufferFromPtNodePos(ptNodePos));
    if (ptNodeParams.isDeleted()) {
        return NOT_A_DICT_POS;
    }
    return mBuffers.get()->getShortcutDictContent()->getShortcutListHeadPos(
            ptNodeParams.getTerminalId());
}

int Ver4PatriciaTriePolicy::getBigramsPositionOfPtNode(const int ptNodePos) const {
    if (ptNodePos == NOT_A_DICT_POS) {
        return NOT_A_DICT_POS;
    }
    const PtNodeParams ptNodeParams(mNodeReader.fetchNodeInfoInBufferFromPtNodePos(ptNodePos));
    if (ptNodeParams.isDeleted()) {
        return NOT_A_DICT_POS;
    }
    return mBuffers.get()->getBigramDictContent()->getBigramListHeadPos(
            ptNodeParams.getTerminalId());
}

bool Ver4PatriciaTriePolicy::addUnigramWord(const int *const word, const int length,
        const int probability, const int *const shortcutTargetCodePoints, const int shortcutLength,
        const int shortcutProbability, const bool isNotAWord, const bool isBlacklisted,
        const int timestamp) {
    if (!mBuffers.get()->isUpdatable()) {
        AKLOGI("Warning: addUnigramWord() is called for non-updatable dictionary.");
        return false;
    }
    if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS) {
        AKLOGE("The dictionary is too large to dynamically update. Dictionary size: %d",
                mDictBuffer->getTailPosition());
        return false;
    }
    DynamicPtReadingHelper readingHelper(mDictBuffer, &mNodeReader);
    readingHelper.initWithPtNodeArrayPos(getRootPosition());
    bool addedNewUnigram = false;
    if (mUpdatingHelper.addUnigramWord(&readingHelper, word, length, probability, isNotAWord,
            isBlacklisted, timestamp,  &addedNewUnigram)) {
        if (addedNewUnigram) {
            mUnigramCount++;
        }
        if (shortcutLength > 0) {
            // Add shortcut target.
            const int wordPos = getTerminalPtNodePositionOfWord(word, length,
                    false /* forceLowerCaseSearch */);
            if (wordPos == NOT_A_DICT_POS) {
                AKLOGE("Cannot find terminal PtNode position to add shortcut target.");
                return false;
            }
            if (!mUpdatingHelper.addShortcutTarget(wordPos, shortcutTargetCodePoints,
                    shortcutLength, shortcutProbability)) {
                AKLOGE("Cannot add new shortcut target. PtNodePos: %d, length: %d, probability: %d",
                        wordPos, shortcutLength, shortcutProbability);
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}

bool Ver4PatriciaTriePolicy::addBigramWords(const int *const word0, const int length0,
        const int *const word1, const int length1, const int probability,
        const int timestamp) {
    if (!mBuffers.get()->isUpdatable()) {
        AKLOGI("Warning: addBigramWords() is called for non-updatable dictionary.");
        return false;
    }
    if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS) {
        AKLOGE("The dictionary is too large to dynamically update. Dictionary size: %d",
                mDictBuffer->getTailPosition());
        return false;
    }
    const int word0Pos = getTerminalPtNodePositionOfWord(word0, length0,
            false /* forceLowerCaseSearch */);
    if (word0Pos == NOT_A_DICT_POS) {
        return false;
    }
    const int word1Pos = getTerminalPtNodePositionOfWord(word1, length1,
            false /* forceLowerCaseSearch */);
    if (word1Pos == NOT_A_DICT_POS) {
        return false;
    }
    bool addedNewBigram = false;
    if (mUpdatingHelper.addBigramWords(word0Pos, word1Pos, probability, timestamp,
            &addedNewBigram)) {
        if (addedNewBigram) {
            mBigramCount++;
        }
        return true;
    } else {
        return false;
    }
}

bool Ver4PatriciaTriePolicy::removeBigramWords(const int *const word0, const int length0,
        const int *const word1, const int length1) {
    if (!mBuffers.get()->isUpdatable()) {
        AKLOGI("Warning: addBigramWords() is called for non-updatable dictionary.");
        return false;
    }
    if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS) {
        AKLOGE("The dictionary is too large to dynamically update. Dictionary size: %d",
                mDictBuffer->getTailPosition());
        return false;
    }
    const int word0Pos = getTerminalPtNodePositionOfWord(word0, length0,
            false /* forceLowerCaseSearch */);
    if (word0Pos == NOT_A_DICT_POS) {
        return false;
    }
    const int word1Pos = getTerminalPtNodePositionOfWord(word1, length1,
            false /* forceLowerCaseSearch */);
    if (word1Pos == NOT_A_DICT_POS) {
        return false;
    }
    if (mUpdatingHelper.removeBigramWords(word0Pos, word1Pos)) {
        mBigramCount--;
        return true;
    } else {
        return false;
    }
}

void Ver4PatriciaTriePolicy::flush(const char *const filePath) {
    if (!mBuffers.get()->isUpdatable()) {
        AKLOGI("Warning: flush() is called for non-updatable dictionary. filePath: %s", filePath);
        return;
    }
    mWritingHelper.writeToDictFile(filePath, mUnigramCount, mBigramCount);
}

void Ver4PatriciaTriePolicy::flushWithGC(const char *const filePath) {
    if (!mBuffers.get()->isUpdatable()) {
        AKLOGI("Warning: flushWithGC() is called for non-updatable dictionary.");
        return;
    }
    mWritingHelper.writeToDictFileWithGC(getRootPosition(), filePath);
}

bool Ver4PatriciaTriePolicy::needsToRunGC(const bool mindsBlockByGC) const {
    if (!mBuffers.get()->isUpdatable()) {
        AKLOGI("Warning: needsToRunGC() is called for non-updatable dictionary.");
        return false;
    }
    if (mBuffers.get()->isNearSizeLimit()) {
        // Additional buffer size is near the limit.
        return true;
    } else if (mHeaderPolicy->getExtendedRegionSize() + mDictBuffer->getUsedAdditionalBufferSize()
            > Ver4DictConstants::MAX_DICT_EXTENDED_REGION_SIZE) {
        // Total extended region size of the trie exceeds the limit.
        return true;
    } else if (mDictBuffer->getTailPosition() >= MIN_DICT_SIZE_TO_REFUSE_DYNAMIC_OPERATIONS
            && mDictBuffer->getUsedAdditionalBufferSize() > 0) {
        // Needs to reduce dictionary size.
        return true;
    } else if (mHeaderPolicy->isDecayingDict()) {
        return ForgettingCurveUtils::needsToDecay(mindsBlockByGC, mUnigramCount, mBigramCount,
                mHeaderPolicy);
    }
    return false;
}

void Ver4PatriciaTriePolicy::getProperty(const char *const query, const int queryLength,
        char *const outResult, const int maxResultLength) {
    const int compareLength = queryLength + 1 /* terminator */;
    if (strncmp(query, UNIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d", mUnigramCount);
    } else if (strncmp(query, BIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d", mBigramCount);
    } else if (strncmp(query, MAX_UNIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d",
                mHeaderPolicy->isDecayingDict() ? ForgettingCurveUtils::MAX_UNIGRAM_COUNT :
                        static_cast<int>(Ver4DictConstants::MAX_DICTIONARY_SIZE));
    } else if (strncmp(query, MAX_BIGRAM_COUNT_QUERY, compareLength) == 0) {
        snprintf(outResult, maxResultLength, "%d",
                mHeaderPolicy->isDecayingDict() ? ForgettingCurveUtils::MAX_BIGRAM_COUNT :
                        static_cast<int>(Ver4DictConstants::MAX_DICTIONARY_SIZE));
    }
}

const UnigramProperty Ver4PatriciaTriePolicy::getUnigramProperty(const int *const codePoints,
        const int codePointCount) const {
    const int ptNodePos = getTerminalPtNodePositionOfWord(codePoints, codePointCount,
            false /* forceLowerCaseSearch */);
    if (ptNodePos == NOT_A_DICT_POS) {
        AKLOGE("fetchUnigramProperty is called for invalid word.");
        return UnigramProperty();
    }
    const PtNodeParams ptNodeParams = mNodeReader.fetchNodeInfoInBufferFromPtNodePos(ptNodePos);
    const ProbabilityEntry probabilityEntry =
            mBuffers.get()->getProbabilityDictContent()->getProbabilityEntry(
                    ptNodeParams.getTerminalId());
    const HistoricalInfo *const historicalInfo = probabilityEntry.getHistoricalInfo();
    // Fetch shortcut information.
    std::vector<std::vector<int> > shortcutTargets;
    std::vector<int> shortcutProbabilities;
    int shortcutPos = getShortcutPositionOfPtNode(ptNodePos);
    if (shortcutPos != NOT_A_DICT_POS) {
        int shortcutTarget[MAX_WORD_LENGTH];
        const ShortcutDictContent *const shortcutDictContent =
                mBuffers.get()->getShortcutDictContent();
        bool hasNext = true;
        while (hasNext) {
            int shortcutTargetLength = 0;
            int shortcutProbability = NOT_A_PROBABILITY;
            shortcutDictContent->getShortcutEntryAndAdvancePosition(MAX_WORD_LENGTH, shortcutTarget,
                    &shortcutTargetLength, &shortcutProbability, &hasNext, &shortcutPos);
            std::vector<int> target(shortcutTarget, shortcutTarget + shortcutTargetLength);
            shortcutTargets.push_back(target);
            shortcutProbabilities.push_back(shortcutProbability);
        }
    }
    return UnigramProperty(ptNodeParams.getCodePoints(), ptNodeParams.getCodePointCount(),
            ptNodeParams.isNotAWord(), ptNodeParams.isBlacklisted(), ptNodeParams.hasBigrams(),
            ptNodeParams.hasShortcutTargets(), ptNodeParams.getProbability(),
            historicalInfo->getTimeStamp(), historicalInfo->getLevel(),
            historicalInfo->getCount(), &shortcutTargets, &shortcutProbabilities);
}

} // namespace latinime
