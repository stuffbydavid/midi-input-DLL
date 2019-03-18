#pragma comment (lib, "Winmm.lib")

#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>
#include <iostream>

#define GMEXPORT extern "C" __declspec (dllexport)

using namespace std;

wstring towstr(const string str) {
	wstring buffer;
	buffer.resize(MultiByteToWideChar(CP_UTF8, 0, &str[0], -1, 0, 0));
	MultiByteToWideChar(CP_UTF8, 0, &str[0], -1, &buffer[0], buffer.size());
	return &buffer[0];
}

string tostr(const wstring wstr) {
	std::string buffer;
	buffer.resize(WideCharToMultiByte(CP_UTF8, 0, &wstr[0], -1, 0, 0, NULL, NULL));
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], -1, &buffer[0], buffer.size(), NULL, NULL);
	return &buffer[0];
}

struct KeyPress {
    // Simple struct for storing key presses.
    int note;
    int velocity;
    DWORD time;
};
struct KeyRelease {
    // Simple struct for storing key releases.
    int note;
    DWORD time;
};
class InputDevice {
    // Class for a MIDI input device.
    public:
      HMIDIIN midiIn;
      MIDIINCAPS info;
      vector<KeyPress> detectPresses;
      vector<KeyPress> usePresses;
      vector<KeyRelease> detectReleases;
      vector<KeyRelease> useReleases;
      DWORD lastMessage;
      int instrument;
      int pitchWheel;
      int control[128];
};
vector<InputDevice> devices;
string retString;
string dName;

static void CALLBACK MidiMessage(HMIDIIN handle, UINT uMsg, DWORD deviceN, DWORD msg, DWORD time) {
    // Message info: http://www.midi.org/techspecs/midimessages.php
    int status = (LOBYTE(LOWORD(msg)) >> 4) & 0x0f;
    int msg1 = HIBYTE(LOWORD(msg));
    int msg2 = LOBYTE(HIWORD(msg));

    if (status == 9 && msg2 == 0) status = 8; // Key press at volume 0 = release
    switch (status) {
        case 8: { // Key release (msg1: note)
            KeyRelease key;
            key.note = msg1;
            key.time = time;
            devices.at(deviceN).detectReleases.push_back(key);
            break;
        }
        case 9: { // Key press (msg1: note, msg2: velocity)
            KeyPress key;
            key.note = msg1;
            key.velocity = msg2;
            key.time = time;
            devices.at(deviceN).detectPresses.push_back(key);
            break;
        }
        case 11: { // Control (msg1: type, msg2: data)
            devices.at(deviceN).control[msg1] = msg2;
            break;
        }
        case 12: { // Patch change (msg1: patch)
            if (time - devices.at(deviceN).lastMessage < 100) break; // Patch change sends two messages? Only first one is correct
            devices.at(deviceN).instrument = msg1;
            devices.at(deviceN).lastMessage = time;
            break;
        }
        case 14: { // Pitch wheel change (msg2: value)
            devices.at(deviceN).pitchWheel = msg2;
            break;
        }
    }
}

void UpdateMidiDevices() {
    // Starts new connections and deletes unavailable.
    vector<MIDIINCAPS> foundDevs;
    for (unsigned int i = 0; i < midiInGetNumDevs(); i++) { // Store
        MIDIINCAPS foundDev;
        bool isNew = true;
        midiInGetDevCaps(i, &foundDev, sizeof(foundDev));
        foundDevs.push_back(foundDev);
        for (unsigned int j = 0; j < devices.size(); j++) {
            if (wcscmp(foundDev.szPname, devices.at(j).info.szPname) == 0) {
                isNew = false;
                break;
            }
        }
        if (isNew) { // If new from last check, start connection
            InputDevice newdev;
            newdev.info = foundDev;
            newdev.lastMessage = 0;
            newdev.instrument = 0;
            newdev.pitchWheel = 64;
            for (int j = 0; j < 128; j++) newdev.control[j] = 0;

            devices.push_back(newdev);
            midiInOpen(&newdev.midiIn, i, (DWORD)MidiMessage, i, CALLBACK_FUNCTION);
            midiInStart(newdev.midiIn);
        }
    }
    for (unsigned int i = 0; i < devices.size(); i++) { // Remove unused
        bool isRemoved = true;
        for (unsigned int j = 0; j < foundDevs.size(); j++) {
            if (wcscmp(devices.at(i).info.szPname, foundDevs.at(j).szPname) == 0) {
                isRemoved = false;
                break;
            }
        }
        if (isRemoved) {
            midiInStop(devices.at(i).midiIn);
            devices.erase(devices.begin() + i);
            i--;
        }
    }
}

BOOL WINAPI DllMain(HANDLE hinstDLL, DWORD dwReason, LPVOID lpvReserved) {
    // DLL main function.
	switch (dwReason) {
        case DLL_PROCESS_ATTACH: {
            UpdateMidiDevices();
            break;
        }
        case DLL_PROCESS_DETACH: {
            for (int i = 0; i < devices.size(); i++) midiInStop(devices.at(i).midiIn);
            break;
        }
	}
	return TRUE;
}

GMEXPORT double MidiInputDevices() {
    // Returns the length of the devices vector (list).
    UpdateMidiDevices();
    return (double)devices.size();
}

GMEXPORT char* MidiInputDeviceName(double n) {
    // Returns product name of the nth item in the devices vector (list).
    if (n < 0 || n >= devices.size()) return "";
	dName = tostr(devices.at(n).info.szPname);
	return &dName[0];
}

GMEXPORT double MidiInputKeyPresses(double n) {
    // Move detected key presses to usable key presses and return the amount.
    if (n < 0 || n >= devices.size()) return 0;
    devices.at(n).usePresses = devices.at(n).detectPresses;
    devices.at(n).detectPresses.clear();
    return (double)devices.at(n).usePresses.size();
}

GMEXPORT double MidiInputKeyPressNote(double n, double key) {
    // Returns the note value in a device's vector of usable key presses.
    if (n < 0 || n >= devices.size()) return 0;
    if (key < 0 || key >= devices.at(n).usePresses.size()) return 0;
    return (double)devices.at(n).usePresses.at(key).note;
}

GMEXPORT double MidiInputKeyPressVelocity(double n, double key) {
    // Returns the velocity value in a device's vector of usable key presses.
    if (n < 0 || n >= devices.size()) return 0;
    if (key < 0 || key >= devices.at(n).usePresses.size()) return 0;
    return (double)devices.at(n).usePresses.at(key).velocity;
}

GMEXPORT double MidiInputKeyPressTime(double n, double key) {
    // Returns the timestamp in a device's vector of usable key presses.
    if (n < 0 || n >= devices.size()) return 0;
    if (key < 0 || key >= devices.at(n).usePresses.size()) return 0;
    return (double)devices.at(n).usePresses.at(key).time;
}

GMEXPORT double MidiInputKeyReleases(double n) {
    // Move detected key releases to usable key releases and return the amount.
    if (n < 0 || n >= devices.size()) return 0;
    devices.at(n).useReleases = devices.at(n).detectReleases;
    devices.at(n).detectReleases.clear();
    return (double)devices.at(n).useReleases.size();
}

GMEXPORT double MidiInputKeyReleaseNote(double n, double key) {
    // Returns the note value in a device's vector of usable key releases.
    if (n < 0 || n >= devices.size()) return 0;
    if (key < 0 || key >= devices.at(n).useReleases.size()) return 0;
    return (double)devices.at(n).useReleases.at(key).note;
}

GMEXPORT double MidiInputKeyReleaseTime(double n, double key) {
    // Returns the timestamp in a device's vector of usable key releases.
    if (n < 0 || n >= devices.size()) return 0;
    if (key < 0 || key >= devices.at(n).useReleases.size()) return 0;
    return (double)devices.at(n).useReleases.at(key).time;
}

GMEXPORT double MidiInputInstrument(double n) {
    // Returns the selected patch (instrument) of a device.
    if (n < 0 || n >= devices.size()) return 0;
    return (double)devices.at(n).instrument;
}

GMEXPORT double MidiInputPitchWheel(double n) {
    // Returns the pitch wheel of a device.
    if (n < 0 || n >= devices.size()) return 64;
    return (double)devices.at(n).pitchWheel;
}

GMEXPORT double MidiInputControl(double n, double control) {
    // Returns a control value of a device.
    if (n < 0 || n >= devices.size()) return 0;
    if (control < 0 || control > 127) return 0;
    return (double)devices.at(n).control[(int)control];
}
