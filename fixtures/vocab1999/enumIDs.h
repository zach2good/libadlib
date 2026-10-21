// enumIDs.h: the test vocabulary of the 1999 compiler compatibility corpus (fixtures/compiler1999).
//
// The fixtures were frozen by running the 1999 compiler with these files as its symbol files, so
// the positions are part of the expectations: append, do not reorder. The 1999 loader ignores the
// value after '=' and starts an enum whose first name has MSG, AGD or ACF at characters 3..5 at
// 4, 1 or 2 (the reserved counts).

#ifndef __enumIDs__
	#define __enumIDs__

enum ID_MSG {
	id_MSG_Ping, id_MSG_Pong, id_MSG_Coin, id_MSG_Done };

enum CollisionObjectID {
	id_COB_Body, id_COB_Bat, id_COB_Glove, id_COB_NUMCOB };

enum Behavior_IDs
	{	id_BEH_DoNothing,
		id_BEH_Walk,
		id_BEH_Swing,
		id_BEH_NUMBEHAVIORS};

enum AgendaItemIDs
 {
	id_AGD_Dummy = id_AGD_NUMRESAGDIDS,	//the reserved items come first
	id_AGD_Wait,
	id_AGD_NUMAGDITEMS
	};

enum ActionFunctionIDs
{
	id_ACF_Dummy = id_ACF_NUMRESACFIDS,	//the reserved actions come first
	id_ACF_PlayTone,
	id_ACF_SetPose,
	id_ACF_ShowText,
	id_ACF_NUMACTIONFUNCTIONS
};

enum DecisionFunctionIDs {
	id_DCF_Choose, id_DCF_IsReady, id_DCF_NUMDECISIONFUNCTIONS };

enum BehaviorSetParamFnIDs {
	id_SET_KeyMonitorMode, id_SET_PurgeFlag, id_SET_NUMBEHSETFNS };

enum InteractorSetParamFnIDs {
	id_SET_InteractorName, id_SET_ActiveState, id_SET_NUMINTSETFNS };

enum Pronouns
{
	id_PRN_Me,
	id_PRT_ViewOwner,	// resolved at run time
	id_PRN_Target,
	id_PRT_LeadRunner,
	id_PRN_NUMPRONOUNS };

enum BALLCOUNTTYPE { BC_OUTS, BC_BALLS, BC_STRIKES };

enum DirectionFlag {
	A_AHEAD, A_BACKWRD, A_LEFT, A_RIGHT };

// The 1999 loader skips the value after '=': id_TV_x is 0 and id_TV_y 1.
enum TestValues { id_TV_x = 5, id_TV_y };

#endif
