/*++

Copyright (c) 2016 Minoca Corp. All Rights Reserved

Module Name:

    core.c

Abstract:

    This module implements the core of the Chalk runtime, including the base
    classes.

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
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

//
// -------------------------------------------------------------------- Globals
//

//
// ------------------------------------------------------------------ Functions
//

CK_ERROR_TYPE
CkpInitializeCore (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine initialize the Chalk VM, creating and wiring up the root
    classes.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    Chalk status.

--*/

{

    PCK_MODULE CoreModule;
    CK_VALUE Value;

    CoreModule = CkpModuleCreate(Vm, NULL);
    if (CoreModule == NULL) {
        return CkErrorNoMemory;
    }

    CK_OBJECT_VALUE(Value, CoreModule);
    CkpDictSet(Vm, Vm->Modules, CK_NULL_VALUE, Value);

    //
    // TODO: Implement CkpInitializeCore.
    //

    return CkSuccess;
}

//
// --------------------------------------------------------- Internal Functions
//

