#pragma once

typedef enum {
    WifiMarauderEventRefreshConsoleOutput = 0,
    WifiMarauderEventStartConsole,
    WifiMarauderEventStartKeyboard,
    WifiMarauderEventSaveSourceMac,
    WifiMarauderEventSaveDestinationMac,
    WifiMarauderEventStartSettingsInit,
    WifiMarauderEventStartLogViewer,
    WifiMarauderEventStartScriptSelect,
    WifiMarauderEventStartSniffPmkidOptions,
    // Marauder's Mate: live scanall feature
    WifiMarauderEventStartScanLive,
    WifiMarauderEventScanApSelected,
    WifiMarauderEventScanRescan,
    WifiMarauderEventScanAction,
    WifiMarauderEventScanStations,
    WifiMarauderEventScanFoxHunt,
    WifiMarauderEventScanJoin,
    WifiMarauderEventScanHosts,
    WifiMarauderEventStartBeaconMon,
    WifiMarauderEventStaDeauth,
    WifiMarauderEventPrevScene
} WifiMarauderCustomEvent;
