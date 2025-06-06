#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <stdio.h>

typedef struct {
    word_t maxUsedFrame;
    word_t emptyTable;
    word_t emptyTableParent;
    word_t bestEvictFrame;
    word_t bestEvictParent;
    word_t bestEvictOffset;
    uint64_t bestEvictPageNumber;
    uint64_t bestDistance;
} FrameSearchResult;

static void clearFrame(uint64_t frameIndex);
static void parseAddress(uint64_t virtualAddress, word_t indices[]);
static uint64_t computeCyclicDistance(uint64_t pageA, uint64_t pageB);
static void unlinkFromParent(word_t parentFrame, word_t childFrame);
static uint64_t traverseTree(uint64_t virtualAddress);
static word_t findFreeOrEvictFrame(uint64_t targetPage, word_t avoidFrame);
static void dfsSearch(word_t currentFrame, int depth, uint64_t targetPage,
                      word_t avoidFrame, word_t parentFrame, word_t parentOffset,
                      uint64_t currentPageNumber, FrameSearchResult* result);



////////////////////////////////// API ////////////////////////////////////////
/*
 * Initialize the virtual memory
 */
void VMinitialize() {
    clearFrame(0);
}

/* reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t *value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE || !value) {
        return 0;
    }
    uint64_t physicalAddress = traverseTree(virtualAddress);
    PMread(physicalAddress, value);
    return 1;
}

/* writes a word to the given virtual address
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */

int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t physicalAddress = traverseTree(virtualAddress);
    PMwrite(physicalAddress, value);
    return 1;
}


/////////////////////////////// HELPERS ///////////////////////////////////////

static void clearFrame(uint64_t frameIndex) {
    for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
        PMwrite(frameIndex * PAGE_SIZE + offset, 0);
    }
}

// parse virtual address into indices per level + offset //
static void parseAddress(uint64_t virtualAddress, word_t indices[]) {
    for (int i = TABLES_DEPTH; i >= 0; i--) {
        indices[i] = virtualAddress & ((1 << OFFSET_WIDTH) - 1);
        virtualAddress >>= OFFSET_WIDTH;
    }
}

// Computes the cyclic distance between two page numbers
static uint64_t computeCyclicDistance(uint64_t pageA, uint64_t pageB) {
    uint64_t diff = (pageA > pageB) ? (pageA - pageB) : (pageB - pageA);
    return (diff < (NUM_PAGES - diff)) ? diff : (NUM_PAGES - diff);
}

static void unlinkFromParent(word_t parentFrame, word_t childFrame) {
    for (uint64_t offset = 0; offset < PAGE_SIZE; ++offset) {
        word_t entry;
        PMread(parentFrame * PAGE_SIZE + offset, &entry);
        if (entry == childFrame) {
            PMwrite(parentFrame * PAGE_SIZE + offset, 0);
            break;
        }
    }
}

////////////////////////// FRAME SEARCH ///////////////////////////////////////


// Traverse the page table tree to resolve virtual address
static uint64_t traverseTree(uint64_t virtualAddress) {
    word_t indices[TABLES_DEPTH + 1];
    parseAddress(virtualAddress, indices);

    word_t currentFrame = 0;
    uint64_t pageNumber = 0;
    for (int level = 0; level < TABLES_DEPTH; ++level) {
        pageNumber = (pageNumber << OFFSET_WIDTH) | indices[level];
    }

    for (int level = 0; level < TABLES_DEPTH; ++level) {
        word_t entry;
        PMread(currentFrame * PAGE_SIZE + indices[level], &entry);
        if (entry == 0) { // PAGE FAULT
            word_t newFrame = findFreeOrEvictFrame(pageNumber, currentFrame);
            if (level < TABLES_DEPTH - 1) { // not a leaf - create table
                clearFrame(newFrame);
            } else { // leaf - restore page
                PMrestore(newFrame, pageNumber);
            }

            PMwrite(currentFrame * PAGE_SIZE + indices[level], newFrame);
            entry = newFrame;
        }

        currentFrame = entry;
    }

    return currentFrame * PAGE_SIZE + indices[TABLES_DEPTH];
}

// Find a free frame or select a frame to evict based on cyclic distance
static word_t findFreeOrEvictFrame(uint64_t targetPage, word_t avoidFrame) {
    FrameSearchResult result = {0};
    dfsSearch(0, 0, targetPage, avoidFrame, 0, 0, 0, &result);

    // Priority 1: Empty table found
    if (result.emptyTable != 0) {
        unlinkFromParent(result.emptyTableParent, result.emptyTable);
        return result.emptyTable;
    }

    // Priority 2: Free frame available
    if (result.maxUsedFrame + 1 < NUM_FRAMES) {
        return result.maxUsedFrame + 1;
    }

    // Priority 3: Evict the farthest page
    // if (result.bestEvictFrame != 0)
    word_t victimFrame;
    PMread(result.bestEvictParent * PAGE_SIZE + result.bestEvictOffset, &victimFrame);
    PMevict(victimFrame, result.bestEvictPageNumber);
    PMwrite(result.bestEvictParent * PAGE_SIZE + result.bestEvictOffset, 0);

    return victimFrame;
}


// recursive DFS search through page table tree and update result accordingly
static void dfsSearch(word_t currentFrame, int depth, uint64_t targetPage,
                      word_t avoidFrame, word_t parentFrame, word_t parentOffset,
                      uint64_t currentPageNumber, FrameSearchResult* result) {

    if (currentFrame > result->maxUsedFrame) {
        result->maxUsedFrame = currentFrame;
    }

//    if (currentFrame == avoidFrame) {
//        return; // skip this frame
//    }

    if (depth == TABLES_DEPTH) {
        // leaf = page frame reached: compute cyclic distance
        uint64_t distance = computeCyclicDistance(targetPage, currentPageNumber);
        if (distance > result->bestDistance) {
            result->bestDistance = distance;
            result->bestEvictFrame = currentFrame;
            result->bestEvictParent = parentFrame;
            result->bestEvictOffset = parentOffset;
            result->bestEvictPageNumber = currentPageNumber;
        }
        return;
    }

    bool isEmpty = true;
    // go through all entries in table, and recursively search children
    for (word_t offset = 0; offset < PAGE_SIZE; ++offset) {
        word_t entry;
        PMread(currentFrame * PAGE_SIZE + offset, &entry);

        if (entry != 0) {
            isEmpty = false;
            dfsSearch(entry, depth + 1, targetPage, avoidFrame, currentFrame, offset,
                      (currentPageNumber << OFFSET_WIDTH) | offset, result);
        }
    }
    // if the entire table frame is empty we can reuse it
    // emptyTable==0 ensures this is done only for the first empty frame found
    if (isEmpty && currentFrame != 0 && currentFrame != avoidFrame && result->emptyTable == 0) {
        result->emptyTable = currentFrame;
        result->emptyTableParent = parentFrame;
    }
}