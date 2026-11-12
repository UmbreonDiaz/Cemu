#pragma once
#include "Cafe/OS/RPL/COSModule.h"

// nn_hpad: GameCube controllers on the Wii U GameCube controller adapter (WUP-028).
// Used by Super Smash Bros. for Wii U. Fed by emulated controllers of type "GameCube Controller".
namespace nn::hpad
{
	COSModule* GetModule();
};
