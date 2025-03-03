/*
 * Copyright (c) 2012, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "os_windows.hpp"
#include "pdh_interface.hpp"
#include "runtime/os.hpp"
#include "utilities/macros.hpp"

// PDH API
typedef PDH_STATUS (WINAPI *PdhAddCounter_Fn)(HQUERY, LPCSTR, DWORD, HCOUNTER*);
typedef PDH_STATUS (WINAPI *PdhOpenQuery_Fn)(LPCWSTR, DWORD, HQUERY*);
typedef DWORD      (WINAPI *PdhCloseQuery_Fn)(HQUERY);
typedef PDH_STATUS (WINAPI *PdhCollectQueryData_Fn)(HQUERY);
typedef DWORD      (WINAPI *PdhGetFormattedCounterValue_Fn)(HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
typedef PDH_STATUS (WINAPI *PdhEnumObjectItems_Fn)(LPCTSTR, LPCTSTR, LPCTSTR, LPTSTR, LPDWORD, LPTSTR, LPDWORD, DWORD, DWORD);
typedef PDH_STATUS (WINAPI *PdhRemoveCounter_Fn)(HCOUNTER);
typedef PDH_STATUS (WINAPI *PdhLookupPerfNameByIndex_Fn)(LPCSTR, DWORD, LPSTR, LPDWORD);
typedef PDH_STATUS (WINAPI *PdhMakeCounterPath_Fn)(PDH_COUNTER_PATH_ELEMENTS*, LPTSTR, LPDWORD, DWORD);
typedef PDH_STATUS (WINAPI *PdhExpandWildCardPath_Fn)(LPCSTR, LPCSTR, PZZSTR, LPDWORD, DWORD);

PdhAddCounter_Fn PdhDll::_PdhAddCounter = nullptr;
PdhOpenQuery_Fn  PdhDll::_PdhOpenQuery = nullptr;
PdhCloseQuery_Fn PdhDll::_PdhCloseQuery = nullptr;
PdhCollectQueryData_Fn PdhDll::_PdhCollectQueryData = nullptr;
PdhGetFormattedCounterValue_Fn PdhDll::_PdhGetFormattedCounterValue = nullptr;
PdhEnumObjectItems_Fn PdhDll::_PdhEnumObjectItems = nullptr;
PdhRemoveCounter_Fn PdhDll::_PdhRemoveCounter = nullptr;
PdhLookupPerfNameByIndex_Fn PdhDll::_PdhLookupPerfNameByIndex = nullptr;
PdhMakeCounterPath_Fn PdhDll::_PdhMakeCounterPath = nullptr;
PdhExpandWildCardPath_Fn PdhDll::_PdhExpandWildCardPath = nullptr;

LONG PdhDll::_critical_section = 0;
LONG PdhDll::_initialized = 0;
LONG PdhDll::_pdh_reference_count = 0;
HMODULE PdhDll::_hModule = nullptr;

void PdhDll::initialize(void) {
  _hModule = os::win32::load_Windows_dll("pdh.dll", nullptr, 0);
  if (nullptr == _hModule) {
    return;
  }
  // The 'A' at the end means the ANSI (not the UNICODE) versions of the methods
  _PdhAddCounter               = (PdhAddCounter_Fn)::GetProcAddress(_hModule, "PdhAddCounterA");
  _PdhOpenQuery                = (PdhOpenQuery_Fn)::GetProcAddress(_hModule, "PdhOpenQueryA");
  _PdhCloseQuery               = (PdhCloseQuery_Fn)::GetProcAddress(_hModule, "PdhCloseQuery");
  _PdhCollectQueryData         = (PdhCollectQueryData_Fn)::GetProcAddress(_hModule, "PdhCollectQueryData");
  _PdhGetFormattedCounterValue = (PdhGetFormattedCounterValue_Fn)::GetProcAddress(_hModule, "PdhGetFormattedCounterValue");
  _PdhEnumObjectItems          = (PdhEnumObjectItems_Fn)::GetProcAddress(_hModule, "PdhEnumObjectItemsA");
  _PdhRemoveCounter            = (PdhRemoveCounter_Fn)::GetProcAddress(_hModule, "PdhRemoveCounter");
  _PdhLookupPerfNameByIndex    = (PdhLookupPerfNameByIndex_Fn)::GetProcAddress(_hModule, "PdhLookupPerfNameByIndexA");
  _PdhMakeCounterPath          = (PdhMakeCounterPath_Fn)::GetProcAddress(_hModule, "PdhMakeCounterPathA");
  _PdhExpandWildCardPath       = (PdhExpandWildCardPath_Fn)::GetProcAddress(_hModule, "PdhExpandWildCardPathA");
  InterlockedExchange(&_initialized, 1);
}

bool PdhDll::PdhDetach(void) {
  LONG prev_ref_count = InterlockedExchangeAdd(&_pdh_reference_count, -1);
  BOOL ret = false;
  if (1 == prev_ref_count) {
    if (_initialized && _hModule != nullptr) {
      ret = FreeLibrary(_hModule);
      if (ret) {
        _hModule = nullptr;
        _PdhAddCounter = nullptr;
        _PdhOpenQuery = nullptr;
        _PdhCloseQuery = nullptr;
        _PdhCollectQueryData = nullptr;
        _PdhGetFormattedCounterValue = nullptr;
        _PdhEnumObjectItems = nullptr;
        _PdhRemoveCounter = nullptr;
        _PdhLookupPerfNameByIndex = nullptr;
        _PdhMakeCounterPath = nullptr;
        _PdhExpandWildCardPath = nullptr;
        InterlockedExchange(&_initialized, 0);
      }
    }
  }
  return ret != 0;
}

bool PdhDll::PdhAttach(void) {
  InterlockedExchangeAdd(&_pdh_reference_count, 1);
  if (1 == _initialized) {
    return true;
  }
  while (InterlockedCompareExchange(&_critical_section, 1, 0) == 1);
  if (0 == _initialized) {
    initialize();
  }
  while (InterlockedCompareExchange(&_critical_section, 0, 1) == 0);
  return (_PdhAddCounter != nullptr && _PdhOpenQuery != nullptr
         && _PdhCloseQuery != nullptr && _PdhCollectQueryData != nullptr
         && _PdhGetFormattedCounterValue != nullptr && _PdhEnumObjectItems != nullptr
         && _PdhRemoveCounter != nullptr && _PdhLookupPerfNameByIndex != nullptr
         && _PdhMakeCounterPath != nullptr && _PdhExpandWildCardPath != nullptr);
}

PDH_STATUS PdhDll::PdhAddCounter(HQUERY hQuery, LPCSTR szFullCounterPath, DWORD dwUserData, HCOUNTER* phCounter) {
  assert(_initialized && _PdhAddCounter != nullptr, "PdhAvailable() not yet called");
  return _PdhAddCounter(hQuery, szFullCounterPath, dwUserData, phCounter);
}

PDH_STATUS PdhDll::PdhOpenQuery(LPCWSTR szDataSource, DWORD dwUserData, HQUERY* phQuery) {
  assert(_initialized && _PdhOpenQuery != nullptr, "PdhAvailable() not yet called");
  return _PdhOpenQuery(szDataSource, dwUserData, phQuery);
}

DWORD PdhDll::PdhCloseQuery(HQUERY hQuery) {
  assert(_initialized && _PdhCloseQuery != nullptr, "PdhAvailable() not yet called");
  return _PdhCloseQuery(hQuery);
}

PDH_STATUS PdhDll::PdhCollectQueryData(HQUERY hQuery) {
  assert(_initialized && _PdhCollectQueryData != nullptr, "PdhAvailable() not yet called");
  return _PdhCollectQueryData(hQuery);
}

DWORD PdhDll::PdhGetFormattedCounterValue(HCOUNTER hCounter, DWORD dwFormat, LPDWORD lpdwType, PPDH_FMT_COUNTERVALUE pValue) {
  assert(_initialized && _PdhGetFormattedCounterValue != nullptr, "PdhAvailable() not yet called");
  return _PdhGetFormattedCounterValue(hCounter, dwFormat, lpdwType, pValue);
}

PDH_STATUS PdhDll::PdhEnumObjectItems(LPCTSTR szDataSource, LPCTSTR szMachineName, LPCTSTR szObjectName,
    LPTSTR mszCounterList, LPDWORD pcchCounterListLength, LPTSTR mszInstanceList,
    LPDWORD pcchInstanceListLength, DWORD dwDetailLevel, DWORD dwFlags) {
  assert(_initialized && _PdhEnumObjectItems != nullptr, "PdhAvailable() not yet called");
  return _PdhEnumObjectItems(szDataSource, szMachineName, szObjectName, mszCounterList, pcchCounterListLength,
    mszInstanceList, pcchInstanceListLength, dwDetailLevel, dwFlags);
}

PDH_STATUS PdhDll::PdhRemoveCounter(HCOUNTER hCounter) {
  assert(_initialized && _PdhRemoveCounter != nullptr, "PdhAvailable() not yet called");
  return _PdhRemoveCounter(hCounter);
}

PDH_STATUS PdhDll::PdhLookupPerfNameByIndex(LPCSTR szMachineName, DWORD dwNameIndex, LPSTR szNameBuffer, LPDWORD pcchNameBufferSize) {
  assert(_initialized && _PdhLookupPerfNameByIndex != nullptr, "PdhAvailable() not yet called");
  return _PdhLookupPerfNameByIndex(szMachineName, dwNameIndex, szNameBuffer, pcchNameBufferSize);
}

PDH_STATUS PdhDll::PdhMakeCounterPath(PDH_COUNTER_PATH_ELEMENTS* pCounterPathElements, LPTSTR szFullPathBuffer, LPDWORD pcchBufferSize, DWORD dwFlags) {
  assert(_initialized && _PdhMakeCounterPath != nullptr, "PdhAvailable() not yet called");
  return _PdhMakeCounterPath(pCounterPathElements, szFullPathBuffer, pcchBufferSize, dwFlags);
}

PDH_STATUS PdhDll::PdhExpandWildCardPath(LPCSTR szDataSource, LPCSTR szWildCardPath, PZZSTR mszExpandedPathList, LPDWORD pcchPathListLength, DWORD dwFlags) {
  assert(_initialized && _PdhExpandWildCardPath != nullptr, "PdhAvailable() not yet called");
  return _PdhExpandWildCardPath(szDataSource, szWildCardPath, mszExpandedPathList, pcchPathListLength, dwFlags);
}

bool PdhDll::PdhStatusFail(PDH_STATUS pdhStat) {
  return pdhStat != ERROR_SUCCESS && pdhStat != PDH_MORE_DATA;
}
