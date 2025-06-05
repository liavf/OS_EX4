#include "VirtualMemory.h"
#include "PhysicalMemory.h"


enum FindFrameReturnVal
{
    CONTINUE,
    EMPTY_PAGE_TABLE,
    BIGGER_DISTANCE,
    BREAK
};

struct frameSearchingVariables
{
    word_t emptyTableIndex = 0;
    word_t maxUsedFrame = 0;
    uint64_t maxDistance = 0;
    uint64_t furthestAwayReferenceAddress = 0;
    uint64_t furthestAwayPageIndex = 0;
};

void parseAddress (uint64_t address, word_t *array);
word_t pageFault (bool isTable, word_t parentTable, uint64_t page);
uint64_t getPhysicalAddress (uint64_t virtualAddress);


word_t
getAddressIteration (uint64_t virtualAddress, const word_t *parsedAddress,
                     word_t currentAddress, int i)
{
  word_t value;
  PMread (currentAddress * PAGE_SIZE + parsedAddress[i], &value);
  if (value == 0)
  {
    value = pageFault (
        i < TABLES_DEPTH - 1, currentAddress,
        virtualAddress >> OFFSET_WIDTH
    );
    PMwrite (currentAddress * PAGE_SIZE + parsedAddress[i], value);
  }
  return value;
}

uint64_t getPhysicalAddress (uint64_t virtualAddress)
{
  word_t parsedAddress[TABLES_DEPTH + 1];
  parseAddress (virtualAddress, parsedAddress);


  word_t currentAddress = 0;

  for (int i = 0; i < TABLES_DEPTH; i++)
  {
    currentAddress = getAddressIteration (virtualAddress, parsedAddress,
                                          currentAddress, i);
  }
  uint64_t physicalAddress = currentAddress * PAGE_SIZE +
                             parsedAddress[TABLES_DEPTH];
  return physicalAddress;
}

FindFrameReturnVal
checkLeaf (uint64_t page, uint64_t intermediatePageValue, frameSearchingVariables &varsStruct)
{
  uint64_t distance = page > intermediatePageValue ?
                      page - intermediatePageValue : intermediatePageValue
                                                     - page;
  uint64_t cyclicalDistance = distance < (NUM_PAGES - distance) ?
                              distance : (NUM_PAGES - distance);

  if (cyclicalDistance > varsStruct.maxDistance)
  {
    varsStruct.maxDistance = cyclicalDistance;
    varsStruct.furthestAwayPageIndex = intermediatePageValue;
    return BIGGER_DISTANCE;
  }
  return CONTINUE;
}

//returns 1 if it is an empty page table
FindFrameReturnVal findFrame (int level, word_t frameIndex, word_t
parentTable, uint64_t page,
                              uint64_t intermediatePageValue,
                              frameSearchingVariables &varsStruct)
{
  //! if i am the frame of the parent table that was just created, then i am
  //! empty but do not want to be evicted
  //update max used frame
  if (frameIndex > varsStruct.maxUsedFrame)
  {
    varsStruct.maxUsedFrame = frameIndex;
  }

  if (level == TABLES_DEPTH)
    return checkLeaf (page, intermediatePageValue, varsStruct);

  //!im a table
  bool isEmpty = true;
  for (uint64_t i = 0; i < PAGE_SIZE; i++)
  {
    word_t value;
    PMread (frameIndex * PAGE_SIZE + i, &value);
    if (value)
    {
      isEmpty = false;
      uint64_t newIntermediatePageValue =
          (intermediatePageValue << OFFSET_WIDTH) + i;
      FindFrameReturnVal returnVal = findFrame (level + 1, value,
                                                parentTable,
                                                page, newIntermediatePageValue,
                                                varsStruct);
      if (returnVal == BREAK) return BREAK;
      if (returnVal == EMPTY_PAGE_TABLE)
      {
        PMwrite (frameIndex * PAGE_SIZE + i, 0);
        return BREAK;
      }
      if (returnVal == BIGGER_DISTANCE)
      {
        varsStruct.furthestAwayReferenceAddress = frameIndex * PAGE_SIZE + i;
      }
    }
  }
  if (isEmpty && frameIndex != parentTable)
  {
    varsStruct.emptyTableIndex = frameIndex;
    return EMPTY_PAGE_TABLE;
  }
  return CONTINUE;
}

void swapIn (bool isTable, uint64_t page, word_t newFrameIndex)
{
  if (isTable)
  {
    for (int i = 0; i < PAGE_SIZE; i++)
    {
      PMwrite (newFrameIndex * PAGE_SIZE + i, 0);
    }
  }
  else
  {
    PMrestore (newFrameIndex, page);
  }
}

word_t pageFault (bool isTable, word_t parentTable, uint64_t page)
{
  frameSearchingVariables varsStruct;

  word_t newFrameIndex;
  findFrame (0, 0, parentTable, page, 0, varsStruct);
  //! figure out what the new frame is according to the vars struct
  if (varsStruct.emptyTableIndex)
  {
    newFrameIndex = varsStruct.emptyTableIndex;
  }
  else if (varsStruct.maxUsedFrame < NUM_FRAMES - 1)
  {
    newFrameIndex = varsStruct.maxUsedFrame + 1;
  }
  else
  {
    PMread (varsStruct.furthestAwayReferenceAddress, &newFrameIndex);
    PMevict (newFrameIndex, varsStruct.furthestAwayPageIndex);
    PMwrite (varsStruct.furthestAwayReferenceAddress, 0);
  }

  swapIn (isTable, page, newFrameIndex);
  return newFrameIndex;
}

void parseAddress (uint64_t address, word_t *array)
{
  for (int i = TABLES_DEPTH; i >= 0; i--)
  {
    array[i] = address & ((1 << OFFSET_WIDTH) - 1);
    address >>= OFFSET_WIDTH;
  }
}

//!API FUNCTIONS:

void VMinitialize ()
{
  for (uint64_t i = 0; i < PAGE_SIZE; i++)
  {
    PMwrite (i, 0);
  }
}

int VMread (uint64_t virtualAddress, word_t *value)
{
  if (!value || virtualAddress >= VIRTUAL_MEMORY_SIZE) return 0;
  uint64_t physicalAddress = getPhysicalAddress (virtualAddress);
  PMread (physicalAddress, value);
  return 1;
}

int VMwrite (uint64_t virtualAddress, word_t value)
{
  if (virtualAddress >= VIRTUAL_MEMORY_SIZE) return 0;
  uint64_t physicalAddress = getPhysicalAddress (virtualAddress);
  PMwrite (physicalAddress, value);
  return 1;
}
