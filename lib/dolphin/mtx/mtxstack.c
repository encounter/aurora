#include <assert.h>
#include <dolphin/mtx.h>

void MTXInitStack(MTXStack* sPtr, u32 numMtx) {
    assert(sPtr && "MTXInitStack():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXInitStack():  'sPtr' contains a NULL ptr to stack memory ");
    assert(numMtx && "MTXInitStack():  'numMtx' is 0 ");

    sPtr->numMtx = numMtx;
    sPtr->stackPtr = 0;
}

MtxPtr MTXPush(MTXStack* sPtr, const Mtx m) {
    assert(sPtr && "MTXPush():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXPush():  'sPtr' contains a NULL ptr to stack memory ");
    assert(m && "MTXPush():  NULL MtxPtr 'm' ");

    if (sPtr->stackPtr == NULL) {
        sPtr->stackPtr = sPtr->stackBase;
        MTXCopy(m, sPtr->stackPtr);
    } else {
        assert(((((uintptr_t)sPtr->stackPtr - (uintptr_t)sPtr->stackBase) / 16) / 3) < (sPtr->numMtx - 1) && "MTXPush():  stack overflow ");
        MTXCopy(m, sPtr->stackPtr + 3);
        sPtr->stackPtr += 3;
    }

    return sPtr->stackPtr;
}

MtxPtr MTXPushFwd(MTXStack* sPtr, const Mtx m) {
    assert(sPtr && "MTXPushFwd():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXPushFwd():  'sPtr' contains a NULL ptr to stack memory ");
    assert(m && "MTXPushFwd():  NULL MtxPtr 'm' ");

    if (sPtr->stackPtr == NULL) {
        sPtr->stackPtr = sPtr->stackBase;
        MTXCopy(m, sPtr->stackPtr);
    } else {
        assert(((((uintptr_t)sPtr->stackPtr - (uintptr_t)sPtr->stackBase) / 16) / 3) < (sPtr->numMtx - 1) && "MTXPushFwd():  stack overflow");
        MTXConcat(sPtr->stackPtr, m, sPtr->stackPtr + 3);
        sPtr->stackPtr += 3;
    }

    return sPtr->stackPtr;
}

MtxPtr MTXPushInv(MTXStack* sPtr, const Mtx m) {
    Mtx mInv;
    assert(sPtr && "MTXPushInv():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXPushInv():  'sPtr' contains a NULL ptr to stack memory ");
    assert(m && "MTXPushInv():  NULL MtxPtr 'm' ");

    MTXInverse(m, mInv);
    if (sPtr->stackPtr == NULL) {
        sPtr->stackPtr = sPtr->stackBase;
        MTXCopy(mInv, sPtr->stackPtr);
    } else {
        assert(((((uintptr_t)sPtr->stackPtr - (uintptr_t)sPtr->stackBase) / 16) / 3) < (sPtr->numMtx - 1) && "MTXPushInv():  stack overflow");
        MTXConcat(mInv, sPtr->stackPtr, sPtr->stackPtr + 3);
        sPtr->stackPtr += 3;
    }

    return sPtr->stackPtr;
}

MtxPtr MTXPushInvXpose(MTXStack* sPtr, const Mtx m) {
    Mtx mIT;
    assert(sPtr && "MTXPushInvXpose():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXPushInvXpose():  'sPtr' contains a NULL ptr to stack memory ");
    assert(m && "MTXPushInvXpose():  NULL MtxPtr 'm' ");

    MTXInverse(m, mIT);
    MTXTranspose(mIT, mIT);
    if (sPtr->stackPtr == NULL) {
        sPtr->stackPtr = sPtr->stackBase;
        MTXCopy(mIT, sPtr->stackPtr);
    } else {
        assert(((((uintptr_t)sPtr->stackPtr - (uintptr_t)sPtr->stackBase) / 16) / 3) < (sPtr->numMtx - 1) && "MTXPushInvXpose():  stack overflow ");
        MTXConcat(sPtr->stackPtr, mIT, sPtr->stackPtr + 3);
        sPtr->stackPtr += 3;
    }

    return sPtr->stackPtr;
}

MtxPtr MTXPop(MTXStack* sPtr) {
    assert(sPtr && "MTXPop():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXPop():  'sPtr' contains a NULL ptr to stack memory ");
    
    if (sPtr->stackPtr == NULL) {
        return NULL;
    }

    if (sPtr->stackBase == sPtr->stackPtr) {
        sPtr->stackPtr = NULL;
        return NULL;
    }

    sPtr->stackPtr -= 3;
    return sPtr->stackPtr;
}

MtxPtr MTXGetStackPtr(const MTXStack* sPtr) {
    assert(sPtr && "MTXGetStackPtr():  NULL MtxStackPtr 'sPtr' ");
    assert(sPtr->stackBase && "MTXGetStackPtr():  'sPtr' contains a NULL ptr to stack memory ");
    return sPtr->stackPtr;
}
