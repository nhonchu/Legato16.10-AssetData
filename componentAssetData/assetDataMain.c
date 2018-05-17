/**
 * @file assetDataMain.c
 *
 * Sample Legato App : Asset Data exchange with AirVantage
 *
 *
 * Nhon Chu. October 2015
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */

#include "legato.h"
#include "interfaces.h"

#define ASSET_NAME				 "Room"
#define INSTANCE_COUNT           2
#define APP_RUNNING_DURATION_SEC 600                                     //run this app for 10min

#define START_AVC                                                        //undefined this, if you have a separate AVC Controller


le_avdata_AssetInstanceRef_t    _assetInstRef[INSTANCE_COUNT];           //reference to the 2 asset instances


char*                           _roomName[INSTANCE_COUNT];               //array of 2 Variable Name
float                           _currentTemperature[INSTANCE_COUNT];     //array of 2 Variable Temperature
bool                            _isAcOn[INSTANCE_COUNT];                 //array of 2 Variable IsAC_on
int                             _targetTemperature[INSTANCE_COUNT];      //array of 2 Setting TargetTemperature

int                             _outsideTemperature = 30;                //assuming it's summer, hot outside!

le_timer_Ref_t                  _tempUpdateTimerRef = NULL;              //reference to temperature update timer


#ifdef START_AVC
le_timer_Ref_t                  _avcTimerRef;
le_avc_StatusEventHandlerRef_t  _avcEventHandlerRef = NULL;              //reference to AirVantage Controller (AVC) Session handler
#endif


void OnWriteSetting(le_avdata_AssetInstanceRef_t instRef, const char *fieldName, void *contextPtr)
{
	int i;
	for (i=0; i<INSTANCE_COUNT; i++)
	{
		if (instRef == _assetInstRef[i])
		{
			if (strcmp(fieldName, "TargetTemperature") == 0)
			{
				LE_INFO("Legato AssetData: Setting Change request: Instance(%d).%s is %d C°", i, fieldName, _targetTemperature[i]);
				int  nTemp;
				le_avdata_GetInt(instRef, fieldName, &nTemp);               //Get the new setting from AirVantage

				if (nTemp != _targetTemperature[i])
				{
					_isAcOn[i] = true; //let's set the AC status to ON
					le_avdata_SetBool(instRef, "IsAC_on", _isAcOn[i]);      //reflect the new value to instance

					_targetTemperature[i] = nTemp;
					LE_INFO("Legato AssetData: Setting Change request: %s %s has been set to %d C°", _roomName[i], fieldName, _targetTemperature[i]);

					le_avdata_SetInt(instRef, fieldName, _targetTemperature[i]);  //reflect the new value to instance
				}                
			}
			break;
		}
	}
}

void OnCommand(le_avdata_AssetInstanceRef_t instRef, const char *fieldName, void *contextPtr)
{
	int i;
	for (i=0; i<INSTANCE_COUNT; i++)
	{
		if (instRef == _assetInstRef[i])
		{
			if (strcmp(fieldName, "TurnOffAC") == 0)
			{
				LE_INFO("Legato AssetData: Execute Command Request: Triggered Instance(%d).TurnOffAC", i);
				_isAcOn[i] = false;    //Execute the commande, just turn AC status to OFF

				le_avdata_SetBool(instRef, "IsAC_on", _isAcOn[i]);  //reflect the new value to instance
			}
			break;
		}
	}
}

float convergeTemperature(float currentTemperature, int targetTemperature)
{
	float fTargetTemp = (float) targetTemperature;

	if (currentTemperature == fTargetTemp)
	{
		return fTargetTemp;
	}

	float fStep = 0.2;

	if (currentTemperature > fTargetTemp)
	{
		currentTemperature -= fStep;
	}
	else
	{
		currentTemperature += fStep;    
	}
	
	return currentTemperature;
}

void updateTemperature(le_timer_Ref_t  timerRef)
{
	int i;
	for (i=0; i< INSTANCE_COUNT; i++)
	{
		if (_isAcOn[i])
		{
			_currentTemperature[i] = convergeTemperature(_currentTemperature[i], _targetTemperature[i]);
		}
		else
		{
			_currentTemperature[i] = convergeTemperature(_currentTemperature[i], _outsideTemperature);
		}

		LE_INFO("Legato AssetData: update, %s temperature is %f °C", _roomName[i], _currentTemperature[i]);
		le_avdata_SetFloat(_assetInstRef[i], "Temperature", _currentTemperature[i]);
	}
}

static void sig_appTermination_cbh(int sigNum)
{
	#ifdef START_AVC
	LE_INFO("Legato AssetData: Close AVC session");
	le_avc_StopSession();

	if (NULL != _avcEventHandlerRef)
	{
		//unregister the handler
		le_avc_RemoveStatusEventHandler(_avcEventHandlerRef);
	}
	#endif
}

#ifdef START_AVC
static void AVsessionHandler
(
	le_avc_Status_t     updateStatus,
	int32_t             totalNumBytes,
	int32_t             progress,
	void*               contextPtr
)
{
	LE_INFO("AVsessionHandler-callback: status %i", updateStatus);
}

static void timerExpiredHandler(le_timer_Ref_t  timerRef)
{
	sig_appTermination_cbh(0);

	LE_INFO("Legato AssetData: Legato AssetDataApp Ended");

	//Quit the app
	exit(EXIT_SUCCESS);
}
#endif

COMPONENT_INIT
{
	int i = 0;

	LE_INFO("Legato AssetData: Start Legato AssetDataApp");

	le_sig_Block(SIGTERM);
    le_sig_SetEventHandler(SIGTERM, sig_appTermination_cbh);
    
	#ifdef START_AVC
	//Start AVC Session
	_avcEventHandlerRef = le_avc_AddStatusEventHandler(AVsessionHandler, NULL);    //register a AVC handler
	le_result_t result = le_avc_StartSession();      //Start AVC session. Note: AVC handler must be registered prior starting a session
	if (LE_FAULT == result)
	{
		le_avc_StopSession();
		le_avc_StartSession();
	}

	LE_INFO("Legato AssetData: Started LWM2M session with AirVantage");


	_avcTimerRef = le_timer_Create("assetDataAppSessionTimer");     //create timer
	le_clk_Time_t    delay = { APP_RUNNING_DURATION_SEC, 0 };    //will stop app in the specified duration
	le_timer_SetInterval(_avcTimerRef, delay);
	le_timer_SetRepeat(_avcTimerRef, 1);                            //set repeat to once


	//set callback function to handle timer expiration event
	le_timer_SetHandler(_avcTimerRef, timerExpiredHandler);


	//start timer
	le_timer_Start(_avcTimerRef);
	#endif

	//Create 2 asset instances
	LE_INFO("Legato AssetData: Create 2 instances AssetData ");

	_assetInstRef[i] = le_avdata_Create(ASSET_NAME); //create instance #1 for asset "Room"
	//Assign default value to asset data fields
	_roomName[i] = (char *) malloc(16);
	strcpy(_roomName[i], "bedroom");
	_currentTemperature[i] = 31.0;
	_isAcOn[i] = false;
	_targetTemperature[i] = 21;

	i++;
	_assetInstRef[i] = le_avdata_Create(ASSET_NAME); //create instance #2 for asset "Room"
	_roomName[i] = (char *) malloc(16);
	strcpy(_roomName[i], "living-room");
	_currentTemperature[i] = 30.0;
	_isAcOn[i] = false;
	_targetTemperature[i] = 21;


	//Register handler for Variables, Settings and Commands
	for (i=0; i< INSTANCE_COUNT; i++)
	{
		//set the variable values
		le_avdata_SetString(_assetInstRef[i], "Name", _roomName[i]);
		le_avdata_SetFloat(_assetInstRef[i], "Temperature", _currentTemperature[i]);
		le_avdata_SetBool(_assetInstRef[i], "IsAC_on", _isAcOn[i]);
		le_avdata_SetInt(_assetInstRef[i], "TargetTemperature", _targetTemperature[i]);

		// call OnWriteSetting() handler whenever the setting "TargetTemperature" is accessed
		le_avdata_AddFieldEventHandler(_assetInstRef[i], "TargetTemperature", OnWriteSetting, NULL);
		// call OnCommand() handler whenever the setting "TurnOffAC" is accessed
		le_avdata_AddFieldEventHandler(_assetInstRef[i], "TurnOffAC", OnCommand, NULL);
	}

	//Set timer to update temperature on a regularyly basis
	_tempUpdateTimerRef = le_timer_Create("tempUpdateTimer");     //create timer

	le_clk_Time_t      interval = { 5, 0 };                      //update temperature every 5 seconds
	le_timer_SetInterval(_tempUpdateTimerRef, interval);
	le_timer_SetRepeat(_tempUpdateTimerRef, 0);                   //set repeat to always

	//set callback function to handle timer expiration event
	le_timer_SetHandler(_tempUpdateTimerRef, updateTemperature);

	//start timer
	le_timer_Start(_tempUpdateTimerRef);
}