// reservedEnumIDs.h: the reserved ids of the 1999 test vocabulary (fixtures/compiler1999).
//
// The 1999 compiler reads this file before enumIDs.h. These names and counts are the engine's own.

#ifndef __ReservedEnumIDs__
	#define __ReservedEnumIDs__

enum reservedAgdIDs {id_AGD_CTimerActivity, id_AGD_NUMRESAGDIDS};

enum reservedACFIDs {id_ACF_SendMessage, id_ACF_SendMsgImmediate, id_ACF_NUMRESACFIDS};

enum reservedMsgIDs {id_MSG_Initiate, id_MSG_ExitBehavior,
					 id_MSG_CellAnimationDone, id_MSG_SyncFrame, id_MSG_NUMRESMSGIDS};

#endif
