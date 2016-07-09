/*++

Copyright (c) 2016 Minoca Corp. All Rights Reserved

Module Name:

    dict.c

Abstract:

    This module implements support for dictionaries (hash tables) within Chalk.

Author:

    Evan Green 28-May-2016

Environment:

    Any

--*/

//
// ------------------------------------------------------------------- Includes
//

#include "chalkp.h"

//
// ---------------------------------------------------------------- Definitions
//

//
// Define the maximum percentage of dictionary hash table entries that can be
// filled before the table is resized. This is roughly percent times ten
// (it's actually 1024ths to turn a divide into a shift).
//

#define DICT_LOAD_FACTOR 768

//
// Define how much bigger to make a dictionary when growing it.
//

#define DICT_GROW_FACTOR 2

//
// Define the factor by which the table has to shrink before resizing. Shrink
// less often than grow to add some hysteresis to things.
//

#define DICT_SHRINK_FACTOR 3

//
// Define the minimum capacity of a dictionary.
//

#define DICT_MIN_CAPACITY 16

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

PCK_DICT_ENTRY
CkpDictFindEntry (
    PCK_DICT Dict,
    CK_VALUE Key
    );

VOID
CkpDictResize (
    PCK_VM Vm,
    PCK_DICT Dict,
    UINTN NewCapacity
    );

BOOL
CkpDictAddEntry (
    PCK_DICT_ENTRY Entries,
    UINTN Capacity,
    CK_VALUE Key,
    CK_VALUE Value
    );

ULONG
CkpHashValue (
    CK_VALUE Value
    );

ULONG
CkpHashObject (
    PCK_OBJECT Object
    );

//
// -------------------------------------------------------------------- Globals
//

//
// ------------------------------------------------------------------ Functions
//

PCK_DICT
CkpDictCreate (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine creates a new dictionary.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    Returns a pointer to the new dictionary on success.

    NULL on allocation failure.

--*/

{

    PCK_DICT Dict;

    Dict = CkAllocate(Vm, sizeof(CK_DICT));
    if (Dict == NULL) {
        return NULL;
    }

    CkpInitializeObject(Vm, &(Dict->Header), CkObjectDict, Vm->Class.Dict);
    Dict->Count = 0;
    Dict->Capacity = 0;
    Dict->Entries = NULL;
    return Dict;
}

CK_VALUE
CkpDictGet (
    PCK_DICT Dict,
    CK_VALUE Key
    )

/*++

Routine Description:

    This routine finds an entry in the given dictionary.

Arguments:

    Dict - Supplies a pointer to the dictionary object.

    Key - Supplies the key to look up on.

Return Value:

    Returns the value at the given key on success.

    CK_UNDEFINED_VALUE if no entry exists in the dictionary for the given key.

--*/

{

    PCK_DICT_ENTRY Entry;

    Entry = CkpDictFindEntry(Dict, Key);
    if (Entry != NULL) {
        return Entry->Value;
    }

    return CK_UNDEFINED_VALUE;
}

VOID
CkpDictSet (
    PCK_VM Vm,
    PCK_DICT Dict,
    CK_VALUE Key,
    CK_VALUE Value
    )

/*++

Routine Description:

    This routine sets the value for the given key in a dictionary.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Dict - Supplies a pointer to the dictionary object.

    Key - Supplies the key to set the value for.

    Value - Supplies the value to set.

Return Value:

    None. On allocation failure the entry is simply not set.

--*/

{

    UINTN NewCapacity;

    if ((Dict->Count + 1) > (Dict->Capacity * DICT_LOAD_FACTOR / 1024)) {
        NewCapacity = Dict->Capacity * DICT_GROW_FACTOR;
        if (NewCapacity < DICT_MIN_CAPACITY) {
            NewCapacity = DICT_MIN_CAPACITY;
        }

        CkpDictResize(Vm, Dict, NewCapacity);
    }

    if (CkpDictAddEntry(Dict->Entries, Dict->Capacity, Key, Value) != FALSE) {
        Dict->Count += 1;
    }

    return;
}

CK_VALUE
CkpDictRemove (
    PCK_VM Vm,
    PCK_DICT Dict,
    CK_VALUE Key
    )

/*++

Routine Description:

    This routine unsets the value for the given key in a dictionary.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Dict - Supplies a pointer to the dictionary object.

    Key - Supplies the key to unset.

Return Value:

    Returns the old value at the key, or CK_NULL_VALUE if no value existed at
    that key.

--*/

{

    UINTN Capacity;
    PCK_DICT_ENTRY Entry;
    CK_VALUE Value;

    Entry = CkpDictFindEntry(Dict, Key);
    if (Entry == NULL) {
        return CK_NULL_VALUE;
    }

    //
    // Remove the entry from the dictionary. Set it to true, which marks it as
    // a deleted slot (as opposed to an empty slot). When searching for a key,
    // the search must continue through a deleted slot, but can stop if an
    // empty slot is found.
    //

    Value = Entry->Value;
    Entry->Key = CK_UNDEFINED_VALUE;
    Entry->Value = CK_TRUE_VALUE;
    Dict->Count -= 1;
    if ((Dict->Capacity > DICT_MIN_CAPACITY) &&
        (Dict->Count /
         (Dict->Capacity / DICT_SHRINK_FACTOR * DICT_LOAD_FACTOR / 1024))) {

        if (CK_IS_OBJECT(Value)) {
            CkpPushRoot(Vm, CK_AS_OBJECT(Value));
        }

        //
        // Shrink it by the grow factor rather than the shrink factor so
        // there's a little extra room even after the resize. Shrink less
        // aggressively than grow.
        //

        Capacity = Dict->Capacity / DICT_GROW_FACTOR;
        if (Capacity < DICT_MIN_CAPACITY) {
            Capacity = DICT_MIN_CAPACITY;
        }

        if (Capacity != Dict->Capacity) {
            CkpDictResize(Vm, Dict, Capacity);
        }

        if (CK_IS_OBJECT(Value)) {
            CkpPopRoot(Vm);
        }
    }

    return Value;
}

//
// --------------------------------------------------------- Internal Functions
//

PCK_DICT_ENTRY
CkpDictFindEntry (
    PCK_DICT Dict,
    CK_VALUE Key
    )

/*++

Routine Description:

    This routine finds an entry in the dictionary corresponding to the given
    key.

Arguments:

    Dict - Supplies a pointer to the dictionary object.

    Key - Supplies the key to find.

Return Value:

    Returns a pointer to the dict entry on success.

    NULL if no such key exists in the dictionary.

--*/

{

    PCK_DICT_ENTRY Entry;
    UINTN Index;
    UINTN Loop;

    if (Dict->Count == 0) {
        return NULL;
    }

    Index = CkpHashValue(Key) % Dict->Capacity;

    //
    // Loop looking for the entry, using open entry linear search on collision.
    // Although there should always be empty slots in the dictionary, some
    // badly timed allocation failures on resize could result in a full dict.
    //

    for (Loop = 0; Loop < Dict->Capacity; Loop += 1) {
        Entry = &(Dict->Entries[Index]);
        if (CK_IS_UNDEFINED(Entry->Key)) {
            if (CK_IS_UNDEFINED(Entry->Value)) {
                break;
            }

        } else if (CkpAreValuesEqual(Entry->Key, Key) != FALSE) {
            return Entry;
        }

        //
        // Move to the next address linearly. Avoid the divide.
        //

        Index += 1;
        if (Index == Dict->Capacity) {
            Index = 0;
        }
    }

    return NULL;
}

VOID
CkpDictResize (
    PCK_VM Vm,
    PCK_DICT Dict,
    UINTN NewCapacity
    )

/*++

Routine Description:

    This routine resizes the given dictionary.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Dict - Supplies a pointer to the dictionary to resize.

    NewCapacity - Supplies the new capacity of the dictionary.

Return Value:

    None.

--*/

{

    UINTN Index;
    PCK_DICT_ENTRY NewEntries;
    PCK_DICT_ENTRY OldEntry;

    CK_ASSERT(NewCapacity > Dict->Count);

    NewEntries = CkAllocate(Vm, NewCapacity * sizeof(CK_DICT_ENTRY));
    if (NewEntries == NULL) {
        return;
    }

    //
    // Zero the entries, which also happens to set all the keys to undefined.
    //

    CkZero(NewEntries, NewCapacity * sizeof(CK_DICT_ENTRY));

    //
    // Re-add all the old entries.
    //

    OldEntry = Dict->Entries;
    for (Index = 0; Index < Dict->Capacity; Index += 1) {
        if (!CK_IS_UNDEFINED(OldEntry->Key)) {
            CkpDictAddEntry(NewEntries,
                            NewCapacity,
                            OldEntry->Key,
                            OldEntry->Value);
        }

        OldEntry += 1;
    }

    //
    // Remove the old array and replace it with the new one.
    //

    if (Dict->Entries != NULL) {
        CkFree(Vm, Dict->Entries);
    }

    Dict->Entries = NewEntries;
    Dict->Capacity = NewCapacity;
    return;
}

BOOL
CkpDictAddEntry (
    PCK_DICT_ENTRY Entries,
    UINTN Capacity,
    CK_VALUE Key,
    CK_VALUE Value
    )

/*++

Routine Description:

    This routine sets the value for the given key in a dictionary.

Arguments:

    Entries - Supplies a pointer to the entries array.

    Capacity - Supplies the size of the entries array.

    Key - Supplies the key to set the value for.

    Value - Supplies the value to set.

Return Value:

    TRUE if the key was newly added.

    FALSE if the key was replaced or not added.

--*/

{

    PCK_DICT_ENTRY Entry;
    UINTN Index;
    UINTN Loop;

    Index = CkpHashValue(Key) % Capacity;

    //
    // Don't do this infinitely in the case that all recent resize attempts
    // have failed to allocate, and the table is now completely full.
    //

    for (Loop = 0; Loop < Capacity; Loop += 1) {
        Entry = Entries + Index;
        if (CK_IS_UNDEFINED(Entry->Key)) {
            Entry->Key = Key;
            Entry->Value = Value;
            return TRUE;

        } else if (CkpAreValuesEqual(Entry->Key, Key) != FALSE) {
            Entry->Value = Value;
            return FALSE;
        }

        Index += 1;
        if (Index == Capacity) {
            Index = 0;
        }
    }

    //
    // This case is only reached if the table is completely full and the
    // element was never added.
    //

    return FALSE;
}

ULONG
CkpHashValue (
    CK_VALUE Value
    )

/*++

Routine Description:

    This routine hashes the given value for insertion into a dictionary.

Arguments:

    Value - Supplies the value to hash.

Return Value:

    Returns a 32-bit hash of the value.

--*/

{

    switch (Value.Type) {
    case CkValueNull:
        return 0;

    //
    // Just truncate the 64 bit value to 32 bits. XORing the two halves is a
    // possibility, but 1) is more work, and 2) causes -1ULL to alias with 0.
    //

    case CkValueInteger:
        return (ULONG)(CK_AS_INTEGER(Value));

    case CkValueObject:
        return CkpHashObject(CK_AS_OBJECT(Value));

    default:
        break;
    }

    return 0;
}

ULONG
CkpHashObject (
    PCK_OBJECT Object
    )

/*++

Routine Description:

    This routine hashes an object.

Arguments:

    Object - Supplies a pointer to the object to hash.

Return Value:

    Returns a 32-bit hash of the object.

--*/

{

    PCK_RANGE Range;

    switch (Object->Type) {

    //
    // Hash the name string.
    //

    case CkObjectClass:
        Object = &(((PCK_CLASS)Object)->Name->Header);

        //
        // Fall through.
        //

    case CkObjectString:
        return ((PCK_STRING_OBJECT)Object)->Hash;

    //
    // Hash the lower bits of the two sides for a range.
    //

    case CkObjectRange:
        Range = (PCK_RANGE)Object;
        return (ULONG)(Range->From.Int) ^ (ULONG)(Range->To.Int);

    default:
        break;
    }

    //
    // Return the pointer itself, which is pretty arbitrary and not necessarily
    // great against collisions, but won't change throughout the lifetime of
    // the object. Skip the lowest 4 bits since the heap probably aligns things
    // to at least 16.
    //

    return (ULONG)((UINTN)Object >> 4);
}

