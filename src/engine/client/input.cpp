/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SDL.h"

#include <iostream>
#include <math.h>

#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>

#include "input.h"

//print >>f, "int inp_key_code(const char *key_name) { int i; if (!strcmp(key_name, \"-?-\")) return -1; else for (i = 0; i < 512; i++) if (!strcmp(key_strings[i], key_name)) return i; return -1; }"

// this header is protected so you don't include it from anywere
#define KEYS_INCLUDE
#include "keynames.h"
#undef KEYS_INCLUDE

static short jx=0, jy=0;

void CInput::AddEvent(int Unicode, int Key, int Flags)
{
	if(m_NumEvents != INPUT_BUFFER_SIZE)
	{
		m_aInputEvents[m_NumEvents].m_Unicode = Unicode;
		m_aInputEvents[m_NumEvents].m_Key = Key;
		m_aInputEvents[m_NumEvents].m_Flags = Flags;
		m_NumEvents++;
	}
}

CInput::CInput()
{
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
	mem_zero(m_aInputState, sizeof(m_aInputState));

	m_InputCurrent = 0;
	m_InputGrabbed = 0;
	m_InputDispatched = false;

	m_LastRelease = 0;
	m_ReleaseDelta = -1;

	m_NumEvents = 0;
}

void CInput::Init()
{
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	SDL_EnableUNICODE(1);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
}

void CInput::MouseRelative(float *x, float *y)
{
	int nx = 0, ny = 0;
	float Sens = g_Config.m_InpMousesens/100.0f;

	if(g_Config.m_InpGrab)
		SDL_GetRelativeMouseState(&nx, &ny);
	else
	{
		if(m_InputGrabbed)
		{
			SDL_GetMouseState(&nx,&ny);
			SDL_WarpMouse(Graphics()->ScreenWidth()/2,Graphics()->ScreenHeight()/2);
			nx -= Graphics()->ScreenWidth()/2; ny -= Graphics()->ScreenHeight()/2;
		}
	}

//#define TRANSFORM(z) (log(abs(z)+1)/LOG_32768 * 200. * Sens * z/abs(z))
#define TRANSFORM(z) (double(z)/200.)
#define LOG_32768 (10.39720770839917964125)
	
	*x = nx*Sens + ( jx!= 0 ? TRANSFORM(jx) : 0);
	*y = ny*Sens + ( jy != 0 ? TRANSFORM(jy) : 0);
}

void CInput::MouseModeAbsolute()
{
	SDL_ShowCursor(1);
	m_InputGrabbed = 0;
	if(g_Config.m_InpGrab)
		SDL_WM_GrabInput(SDL_GRAB_OFF);
}

void CInput::MouseModeRelative()
{
	SDL_ShowCursor(0);
	m_InputGrabbed = 1;
	if(g_Config.m_InpGrab)
		SDL_WM_GrabInput(SDL_GRAB_ON);
}

int CInput::MouseDoubleClick()
{
	if(m_ReleaseDelta >= 0 && m_ReleaseDelta < (time_freq() >> 2))
	{
		m_LastRelease = 0;
		m_ReleaseDelta = -1;
		return 1;
	}
	return 0;
}

void CInput::ClearKeyStates()
{
	mem_zero(m_aInputState, sizeof(m_aInputState));
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
}

int CInput::KeyState(int Key)
{
	return m_aInputState[m_InputCurrent][Key];
}

int CInput::Update()
{
  static SDL_Joystick *joystick = NULL;
	if(m_InputGrabbed && !Graphics()->WindowActive())
		MouseModeAbsolute();
	
	if ( joystick == NULL )
	  {
	    if (SDL_NumJoysticks() == 0)
	      {
		std::cerr << "no joystick found" << std::endl;
		abort();
	      }
	    SDL_JoystickEventState(SDL_ENABLE);
	    joystick = SDL_JoystickOpen(0);
	    if ( NULL == joystick )
	      {
		std::cerr << "Can't open joystick" << std::endl;
		abort();
	      }
	  }

	/*if(!input_grabbed && Graphics()->WindowActive())
		Input()->MouseModeRelative();*/

	if(m_InputDispatched)
	{
		// clear and begin count on the other one
		m_InputCurrent^=1;
		mem_zero(&m_aInputCount[m_InputCurrent], sizeof(m_aInputCount[m_InputCurrent]));
		mem_zero(&m_aInputState[m_InputCurrent], sizeof(m_aInputState[m_InputCurrent]));
		m_InputDispatched = false;
	}

	{
		int i;
		Uint8 *pState = SDL_GetKeyState(&i);
		if(i >= KEY_LAST)
			i = KEY_LAST-1;
		mem_copy(m_aInputState[m_InputCurrent], pState, i);
	}

	// these states must always be updated manually because they are not in the GetKeyState from SDL
	int i = SDL_GetMouseState(NULL, NULL);
	if(i&SDL_BUTTON(1)) m_aInputState[m_InputCurrent][KEY_MOUSE_1] = 1; // 1 is left
	if(i&SDL_BUTTON(3)) m_aInputState[m_InputCurrent][KEY_MOUSE_2] = 1; // 3 is right
	if(i&SDL_BUTTON(2)) m_aInputState[m_InputCurrent][KEY_MOUSE_3] = 1; // 2 is middle
	if(i&SDL_BUTTON(4)) m_aInputState[m_InputCurrent][KEY_MOUSE_4] = 1;
	if(i&SDL_BUTTON(5)) m_aInputState[m_InputCurrent][KEY_MOUSE_5] = 1;
	if(i&SDL_BUTTON(6)) m_aInputState[m_InputCurrent][KEY_MOUSE_6] = 1;
	if(i&SDL_BUTTON(7)) m_aInputState[m_InputCurrent][KEY_MOUSE_7] = 1;
	if(i&SDL_BUTTON(8)) m_aInputState[m_InputCurrent][KEY_MOUSE_8] = 1;
	if(i&SDL_BUTTON(9)) m_aInputState[m_InputCurrent][KEY_MOUSE_9] = 1;

	{
		SDL_Event Event;

		while(SDL_PollEvent(&Event))
		{
			int Key = -1;
			int Action = IInput::FLAG_PRESS;
			switch (Event.type)
			{
				// handle keys
				case SDL_KEYDOWN:
					// skip private use area of the BMP(contains the unicodes for keyboard function keys on MacOS)
					if(Event.key.keysym.unicode < 0xE000 || Event.key.keysym.unicode > 0xF8FF)	// ignore_convention
						AddEvent(Event.key.keysym.unicode, 0, 0); // ignore_convention
					Key = Event.key.keysym.sym; // ignore_convention
					break;
				case SDL_KEYUP:
					Action = IInput::FLAG_RELEASE;
					Key = Event.key.keysym.sym; // ignore_convention
					break;

				// handle mouse buttons
				case SDL_MOUSEBUTTONUP:
					Action = IInput::FLAG_RELEASE;

					if(Event.button.button == 1) // ignore_convention
					{
						m_ReleaseDelta = time_get() - m_LastRelease;
						m_LastRelease = time_get();
					}

					// fall through
				case SDL_MOUSEBUTTONDOWN:
					if(Event.button.button == SDL_BUTTON_LEFT) Key = KEY_MOUSE_1; // ignore_convention
					if(Event.button.button == SDL_BUTTON_RIGHT) Key = KEY_MOUSE_2; // ignore_convention
					if(Event.button.button == SDL_BUTTON_MIDDLE) Key = KEY_MOUSE_3; // ignore_convention
					if(Event.button.button == SDL_BUTTON_WHEELUP) Key = KEY_MOUSE_WHEEL_UP; // ignore_convention
					if(Event.button.button == SDL_BUTTON_WHEELDOWN) Key = KEY_MOUSE_WHEEL_DOWN; // ignore_convention
					if(Event.button.button == 6) Key = KEY_MOUSE_6; // ignore_convention
					if(Event.button.button == 7) Key = KEY_MOUSE_7; // ignore_convention
					if(Event.button.button == 8) Key = KEY_MOUSE_8; // ignore_convention
					if(Event.button.button == 9) Key = KEY_MOUSE_9; // ignore_convention
					break;

			case SDL_JOYAXISMOTION:

			  if ( /*Event.jaxis.axis == 0 || */ Event.jaxis.axis == 2 )
			    {
			      jx = Event.jaxis.value;
			    }
			  else if (/* Event.jaxis.axis == 1 || */Event.jaxis.axis == 3 ) 
			    {
			      jy = Event.jaxis.value;
			    }
			  
			  break;

			case SDL_JOYHATMOTION:

			  switch(Event.jhat.value)
			    {
			    case SDL_HAT_LEFTUP:
			      m_aInputState[m_InputCurrent][KEY_JS_RIGHT] = 1; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_DOWN] = 1; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_LEFT].m_Presses++; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_PRESS);
			      m_aInputCount[m_InputCurrent][KEY_JS_UP].m_Presses++; AddEvent(0, KEY_JS_UP, IInput::FLAG_PRESS);
			      
			      break;
			    case SDL_HAT_LEFTDOWN:
			      m_aInputState[m_InputCurrent][KEY_JS_RIGHT] = 1; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_UP] = 1; AddEvent(0, KEY_JS_UP, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_LEFT].m_Presses++; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_PRESS);
			      m_aInputCount[m_InputCurrent][KEY_JS_DOWN].m_Presses++; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_PRESS);
			      
			      break;
			    case SDL_HAT_RIGHTUP:
			      m_aInputState[m_InputCurrent][KEY_JS_LEFT] = 1; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_DOWN] = 1; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_RIGHT].m_Presses++; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_PRESS);
			      m_aInputCount[m_InputCurrent][KEY_JS_UP].m_Presses++; AddEvent(0, KEY_JS_UP, IInput::FLAG_PRESS);
			      
			      break;
			    case SDL_HAT_RIGHTDOWN:
			      m_aInputState[m_InputCurrent][KEY_JS_LEFT] = 1; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_UP] = 1; AddEvent(0, KEY_JS_UP, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_RIGHT].m_Presses++; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_PRESS);
			      m_aInputCount[m_InputCurrent][KEY_JS_DOWN].m_Presses++; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_PRESS);

			      break;
			    case SDL_HAT_UP:
			      m_aInputState[m_InputCurrent][KEY_JS_LEFT] = 1; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_RIGHT] = 1; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_DOWN] = 1; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_UP].m_Presses++; AddEvent(0, KEY_JS_UP, IInput::FLAG_PRESS);
			      
			      break;
			    case SDL_HAT_DOWN:
			      m_aInputState[m_InputCurrent][KEY_JS_LEFT] = 1; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_RIGHT] = 1; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_UP] = 1; AddEvent(0, KEY_JS_UP, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_DOWN].m_Presses++; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_PRESS);
			      
			      break;
			    case SDL_HAT_LEFT:
			      m_aInputState[m_InputCurrent][KEY_JS_DOWN] = 1; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_RIGHT] = 1; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_UP] = 1; AddEvent(0, KEY_JS_UP, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_LEFT].m_Presses++; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_PRESS);
			      
			      break;
			    case SDL_HAT_RIGHT:
			      m_aInputState[m_InputCurrent][KEY_JS_DOWN] = 1; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_LEFT] = 1; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_UP] = 1; AddEvent(0, KEY_JS_UP, IInput::FLAG_RELEASE);
			      m_aInputCount[m_InputCurrent][KEY_JS_RIGHT].m_Presses++; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_PRESS);
			      
			      break;
			    default:
			      m_aInputState[m_InputCurrent][KEY_JS_DOWN] = 1; AddEvent(0, KEY_JS_DOWN, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_LEFT] = 1; AddEvent(0, KEY_JS_LEFT, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_UP] = 1; AddEvent(0, KEY_JS_UP, IInput::FLAG_RELEASE);
			      m_aInputState[m_InputCurrent][KEY_JS_RIGHT] = 1; AddEvent(0, KEY_JS_RIGHT, IInput::FLAG_RELEASE);

			    }
			 
			  break;

			case SDL_JOYBUTTONUP:

			  Action = IInput::FLAG_RELEASE;

			  // fall through
			case SDL_JOYBUTTONDOWN:

			  if (Event.jbutton.button == 0 ) Key = KEY_JS_0;
			  if (Event.jbutton.button == 1 ) Key = KEY_JS_1;
			  if (Event.jbutton.button == 2 ) Key = KEY_JS_2;
			  if (Event.jbutton.button == 3 ) Key = KEY_JS_3;
			  if (Event.jbutton.button == 4 ) Key = KEY_JS_4;
			  if (Event.jbutton.button == 5 ) Key = KEY_JS_5;
			  if (Event.jbutton.button == 6 ) Key = KEY_JS_6;
			  if (Event.jbutton.button == 7 ) Key = KEY_JS_7;
			  if (Event.jbutton.button == 8 ) Key = KEY_JS_8;
			  if (Event.jbutton.button == 9 ) Key = KEY_JS_9;
			  
			  break;
				// other messages
				case SDL_QUIT:
					return 1;
			}

			//
			if(Key != -1)
			{
				m_aInputCount[m_InputCurrent][Key].m_Presses++;
				if(Action == IInput::FLAG_PRESS)
					m_aInputState[m_InputCurrent][Key] = 1;
				AddEvent(0, Key, Action);
			}

		}
	}

	return 0;
}


IEngineInput *CreateEngineInput() { return new CInput; }
