#pragma once
#include <objbase.h>

HRESULT OleInitialize(LPVOID pvReserved);
void OleUninitialize();
HRESULT DoDragDrop(IDataObject* pDataObj, IDropSource* pDropSource, DWORD dwOKEffects, LPDWORD pdwEffect);
