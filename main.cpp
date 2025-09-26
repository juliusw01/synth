#include <iostream>
#include <vector>
#include <atomic>
#include <cmath>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <map>
#include "olcNoiseMaker.h"

using namespace std;

#define FTYPE double
#include "olcNoiseMaker.h"


namespace synth
{
	//////////////////////////////////////////////////////////////////////////////
	// Utilities

	// Converts frequency (Hz) to angular velocity
	FTYPE w(const FTYPE dHertz)
	{
		return dHertz * 2.0 * PI;
	}

	// A basic note
	struct note
	{
		int id;		// Position in scale
		FTYPE on;	// Time note was activated
		FTYPE off;	// Time note was deactivated
		bool active;
		int channel;

		note()
		{
			id = 0;
			on = 0.0;
			off = 0.0;
			active = false;
			channel = 0;
		}
	};

	//////////////////////////////////////////////////////////////////////////////
	// Multi-Function Oscillator
	const int OSC_SINE = 0;
	const int OSC_SQUARE = 1;
	const int OSC_TRIANGLE = 2;
	const int OSC_SAW_ANA = 3;
	const int OSC_SAW_DIG = 4;
	const int OSC_NOISE = 5;

	FTYPE osc(const FTYPE dTime, const FTYPE dHertz, const int nType = OSC_SINE,
		const FTYPE dLFOHertz = 0.0, const FTYPE dLFOAmplitude = 0.0, FTYPE dCustom = 50.0)
	{

		FTYPE dFreq = w(dHertz) * dTime + dLFOAmplitude * dHertz * (sin(w(dLFOHertz) * dTime));// osc(dTime, dLFOHertz, OSC_SINE);

		switch (nType)
		{
		case OSC_SINE: // Sine wave bewteen -1 and +1
			return sin(dFreq);

		case OSC_SQUARE: // Square wave between -1 and +1
			return sin(dFreq) > 0 ? 1.0 : -1.0;

		case OSC_TRIANGLE: // Triangle wave between -1 and +1
			return asin(sin(dFreq)) * (2.0 / PI);

		case OSC_SAW_ANA: // Saw wave (analogue / warm / slow)
		{
			FTYPE dOutput = 0.0;
			for (FTYPE n = 1.0; n < dCustom; n++)
				dOutput += (sin(n*dFreq)) / n;
			return dOutput * (2.0 / PI);
		}

		case OSC_SAW_DIG:
			return (2.0 / PI) * (dHertz * PI * fmod(dTime, 1.0 / dHertz) - (PI / 2.0));

		case OSC_NOISE:
			return 2.0 * ((FTYPE)rand() / (FTYPE)RAND_MAX) - 1.0;

		default:
			return 0.0;
		}
	}

	//////////////////////////////////////////////////////////////////////////////
	// Scale to Frequency conversion

	const int SCALE_DEFAULT = 0;

	FTYPE scale(const int nNoteID, const int nScaleID = SCALE_DEFAULT)
	{
		switch (nScaleID)
		{
		case SCALE_DEFAULT: default:
			return 256 * pow(1.0594630943592952645618252949463, nNoteID);
		}
	}


	//////////////////////////////////////////////////////////////////////////////
	// Envelopes

	struct envelope
	{
		virtual FTYPE amplitude(const FTYPE dTime, const FTYPE dTimeOn, const FTYPE dTimeOff) = 0;
	};

	struct envelope_adsr : public envelope
	{
		FTYPE dAttackTime;
		FTYPE dDecayTime;
		FTYPE dSustainAmplitude;
		FTYPE dReleaseTime;
		FTYPE dStartAmplitude;

		envelope_adsr()
		{
			dAttackTime = 0.1;
			dDecayTime = 0.1;
			dSustainAmplitude = 1.0;
			dReleaseTime = 0.2;
			dStartAmplitude = 1.0;
		}

		virtual FTYPE amplitude(const FTYPE dTime, const FTYPE dTimeOn, const FTYPE dTimeOff)
		{
			FTYPE dAmplitude = 0.0;
			FTYPE dReleaseAmplitude = 0.0;

			if (dTimeOn > dTimeOff) // Note is on
			{
				FTYPE dLifeTime = dTime - dTimeOn;

				if (dLifeTime <= dAttackTime)
					dAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;

				if (dLifeTime > dAttackTime && dLifeTime <= (dAttackTime + dDecayTime))
					dAmplitude = ((dLifeTime - dAttackTime) / dDecayTime) * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;

				if (dLifeTime > (dAttackTime + dDecayTime))
					dAmplitude = dSustainAmplitude;
			}
			else // Note is off
			{
				FTYPE dLifeTime = dTimeOff - dTimeOn;

				if (dLifeTime <= dAttackTime)
					dReleaseAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;

				if (dLifeTime > dAttackTime && dLifeTime <= (dAttackTime + dDecayTime))
					dReleaseAmplitude = ((dLifeTime - dAttackTime) / dDecayTime) * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;

				if (dLifeTime > (dAttackTime + dDecayTime))
					dReleaseAmplitude = dSustainAmplitude;

				dAmplitude = ((dTime - dTimeOff) / dReleaseTime) * (0.0 - dReleaseAmplitude) + dReleaseAmplitude;
			}

			// Amplitude should not be negative
			if (dAmplitude <= 0.100)
				dAmplitude = 0.0;

			return dAmplitude;
		}
	};

	FTYPE env(const FTYPE dTime, envelope &env, const FTYPE dTimeOn, const FTYPE dTimeOff)
	{
		return env.amplitude(dTime, dTimeOn, dTimeOff);
	}


	struct instrument_base
	{
		FTYPE dVolume;
		synth::envelope_adsr env;
		virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished) = 0;
	};

	struct instrument_bell : public instrument_base
	{
		instrument_bell()
		{
			env.dAttackTime = 0.01;
			env.dDecayTime = 1.0;
			env.dSustainAmplitude = 0.0;
			env.dReleaseTime = 1.0;

			dVolume = 1.0;
		}

		virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
		{
			FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
			if (dAmplitude <= 0.0) bNoteFinished = true;

			FTYPE dSound =
				+ 1.00 * synth::osc(n.on - dTime, synth::scale(n.id + 12), synth::OSC_SINE, 5.0, 0.001)
				+ 0.50 * synth::osc(n.on - dTime, synth::scale(n.id + 24))
				+ 0.25 * synth::osc(n.on - dTime, synth::scale(n.id + 36));

			return dAmplitude * dSound * dVolume;
		}

	};

	struct instrument_bell8 : public instrument_base
	{
		instrument_bell8()
		{
			env.dAttackTime = 0.01;
			env.dDecayTime = 0.5;
			env.dSustainAmplitude = 0.8;
			env.dReleaseTime = 1.0;

			dVolume = 1.0;
		}

		virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
		{
			FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
			if (dAmplitude <= 0.0) bNoteFinished = true;

			FTYPE dSound =
				+1.00 * synth::osc(n.on - dTime, synth::scale(n.id), synth::OSC_SQUARE, 5.0, 0.001)
				+ 0.50 * synth::osc(n.on - dTime, synth::scale(n.id + 12))
				+ 0.25 * synth::osc(n.on - dTime, synth::scale(n.id + 24));

			return dAmplitude * dSound * dVolume;
		}

	};

	struct instrument_harmonica : public instrument_base
	{
		instrument_harmonica()
		{
			env.dAttackTime = 0.05;
			env.dDecayTime = 1.0;
			env.dSustainAmplitude = 0.95;
			env.dReleaseTime = 0.1;

			dVolume = 1.0;
		}

		virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
		{
			FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
			if (dAmplitude <= 0.0) bNoteFinished = true;

			FTYPE dSound =
				//+ 1.0  * synth::osc(n.on - dTime, synth::scale(n.id-12), synth::OSC_SAW_ANA, 5.0, 0.001, 100)
				+ 1.00 * synth::osc(n.on - dTime, synth::scale(n.id), synth::OSC_SQUARE, 5.0, 0.001)
				+ 0.50 * synth::osc(n.on - dTime, synth::scale(n.id + 12), synth::OSC_SQUARE)
				+ 0.05  * synth::osc(n.on - dTime, synth::scale(n.id + 24), synth::OSC_NOISE);

			return dAmplitude * dSound * dVolume;
		}

	};

}

vector<synth::note> vecNotes;
mutex muxNotes;
synth::instrument_bell instBell;
synth::instrument_harmonica instHarm;



typedef bool(*lambda)(synth::note const& item);
template<class T>
void safe_remove(T &v, lambda f)
{
	auto n = v.begin();
	while (n != v.end())
		if (!f(*n))
			n = v.erase(n);
		else
			++n;
}

// Function used by olcNoiseMaker to generate sound waves
// Returns amplitude (-1.0 to +1.0) as a function of time
FTYPE MakeNoise(int nChannel, FTYPE dTime)
{
	unique_lock<mutex> lm(muxNotes);
	FTYPE dMixedOutput = 0.0;

	for (auto &n : vecNotes)
	{
		bool bNoteFinished = false;
		FTYPE dSound = 0;
		if(n.channel == 2)
			dSound = instBell.sound(dTime, n, bNoteFinished);
		if (n.channel == 1)
			dSound = instHarm.sound(dTime, n, bNoteFinished) * 0.5;
		dMixedOutput += dSound;

		if (bNoteFinished && n.off > n.on)
        {
            cout << "Note " << n.id << " finished, setting inactive" << endl;
			n.active = false;
        }
	}

	// Woah! Modern C++ Overload!!!
	safe_remove<vector<synth::note>>(vecNotes, [](synth::note const& item) { return item.active; });


	return dMixedOutput * 0.2;
}

bool GetKeyPress(char &key)
{
    struct termios oldt, newt;
    //char ch = 0;
    tcgetattr(STDIN_FILENO, &oldt); // Terminal-Settings sichern
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // Eingabeverhalten ändern
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); // Terminal auf neue Settings setzen
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set); // Setze den Satz von Eingabekanälen zurück
    FD_SET(STDIN_FILENO, &set); // Füge die Standard-Eingabe (Tastatur) zum Satz hinzu
    timeout.tv_sec = 0; // Keine Pause, sofortige Rückgabe
    timeout.tv_usec = 500000;
    // select() wartet auf Eingabe, aber blockiert nicht
    int ready = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    if (ready) {
        key = getchar(); // Tasteneingabe ohne zu blockieren
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return true;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Terminal auf ursprüngliche Settings zurücksetzen
    return false;
}

int main()
{
	// Shameless self-promotion
	wcout << "www.OneLoneCoder.com - Synthesizer Part 3" << endl << "Multiple Oscillators with Polyphony" << endl << endl;

	// Get all sound hardware
	vector<wstring> devices = olcNoiseMaker<short>::Enumerate();

	// Display findings
	//for (auto d : devices) wcout << "Found Output Device: " << d << endl;
	//wcout << "Using Device: " << devices[0] << endl;

	// Display a keyboard
	wcout << endl <<
		"|   |   |   |   |   | |   |   |   |   | |   | |   |   |   |" << endl <<
		"|   | S |   |   | F | | G |   |   | J | | K | | L |   |   |" << endl <<
		"|   |___|   |   |___| |___|   |   |___| |___| |___|   |   |__" << endl <<
		"|     |     |     |     |     |     |     |     |     |     |" << endl <<
		"|  Y  |  X  |  C  |  V  |  B  |  N  |  M  |  ,  |  .  |  -  |" << endl <<
		"|_____|_____|_____|_____|_____|_____|_____|_____|_____|_____|" << endl << endl;


	// Create sound machine!!
	olcNoiseMaker<FTYPE> sound(devices[0], 44100, 1, 8, 512);

	// Link noise function with sound machine
	sound.SetUserFunction(MakeNoise);

	char keyboard[129];
	memset(keyboard, ' ', 127);
	keyboard[128] = '\0';

	auto clock_old_time = chrono::high_resolution_clock::now();
	auto clock_real_time = chrono::high_resolution_clock::now();
	double dElapsedTime = 0.0;

    while (1)
    {
        bool nKeyState;
        char key;

        nKeyState = GetKeyPress(key);
        double dTimeNow = sound.GetTime();

        // Mapping von Tasten auf Noten
        map<char, int> keyToNote = {
            {'y', 0}, {'s', 1}, {'x', 2}, {'d', 3}, {'c', 4}, {'v', 5},
            {'g', 6}, {'b', 7}, {'h', 8}, {'n', 9}, {'j', 10}, {'m', 11},
            {'k', 12}, {',', 13}, {'l', 14}, {'.', 15}, {'-', 16}
        };

        // Prüfe, ob die gedrückte Taste in der Noten-Tabelle existiert
        if (keyToNote.find(key) != keyToNote.end())
        {
            int noteID = keyToNote[key];

            muxNotes.lock();
            auto noteFound = find_if(vecNotes.begin(), vecNotes.end(),
                [&noteID](synth::note const& item) { return item.id == noteID; });

            if (noteFound == vecNotes.end())
            {
                // Neue Note hinzufügen, wenn sie noch nicht aktiv ist
                if (nKeyState)
                {
                    synth::note n;
                    n.id = noteID;
                    n.on = dTimeNow;
                    n.channel = 1;
                    n.active = true;
                    vecNotes.emplace_back(n);
                    cout << "New note added: " << key << endl;
                }
            }
            else
            {
                // Wenn die Taste losgelassen wird, beende die Note
                if (!nKeyState && noteFound->off < noteFound->on)
                {
                    noteFound->off = dTimeNow;
                    cout << "Note removed: " << key << endl;
                }
            }
            muxNotes.unlock();
        }

        muxNotes.lock();
        vecNotes.erase(
            remove_if(vecNotes.begin(), vecNotes.end(),
                [dTimeNow](synth::note const& n)
                {
                    return (n.off > 0 && (dTimeNow - n.off) > 0.1);
                }),
            vecNotes.end()
        );
        muxNotes.unlock();

        cout << "\rAktive Noten: " << vecNotes.size() << "   ";
    }
	return 0;
}
