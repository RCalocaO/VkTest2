
#include "pch.h"
#include "RCVulkan.h"
#include "../RCUtils/RCUtilsMath.h"

#pragma comment(lib, "glfw3.lib")


#include "imgui.h"

// GHJOTL.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <time.h>

#include "../GHJOTL/GHJOTL.h"

struct FGameUI : FGame
{
	void PrintScenarioInfo()
	{
		//std::cout << "***** ROUND " << Round << "*****" << std::endl;
		//ImGui::Text("*** Characters ***");
		//std::cout << "\t*** Characters" << std::endl;
		for (FCharacterInstance* C : Scenario->CharacterInstances)
		{
			char s[64];
			//std::cout << "\t\t" <<  << " ";
			ImGui::Begin(C->Character->Name.c_str());

			if (C->IsExhausted())
			{
				sprintf(s, "%s EXHAUSTED!", C->Character->Name.c_str());
			}
			else
			{
				sprintf(s, "%s HP %d!", C->Character->Name.c_str(), C->HP);
			}
			ImGui::Text(s);
			ImGui::End();
		}

		//ImGui::Text("*** Monsters ***");
		//std::cout << "\t*** Monsters" << std::endl;
		for (auto& Pair : Scenario->MonsterInstances)
		{
			FMonster* Monster = Pair.first;
			for (FMonsterInstance* Instance : Pair.second)
			{
				ImGui::Begin(Instance->Name.c_str());
				//std::cout << "\t\t" << Instance->Name << " " << "HP " << Instance->HP << std::endl;
				char s[64];
				sprintf(s, "%s HP %d!", Instance->Name.c_str(), Instance->HP);
				ImGui::Text(s);
				ImGui::End();
			}
		}

		ImGui::Begin("Game Info");
		{
			char s[64];
			sprintf(s, "Round %d", Scenario->Round);
			ImGui::Text(s);
		}
	}
};


static FGameUI Game;

void Tick(float DeltaTime)
{
	Game.PrintScenarioInfo();
	if (!Game.IsScenarioFinishedOrAllCharactersExhausted())
	{
		Game.PlayRound(DeltaTime);
	}
	else
	{
		if (Game.Scenario->AreAllCharactersExhausted())
		{
			ImGui::Text("***** YOU LOSE *****");
		}
		else
		{
			ImGui::Text("***** YOU WIN! *****");
		}
	}
}

void RenderJOTL(SVulkan::SDevice& Device, FStagingBufferManager& StagingMgr, FDescriptorCache& DescriptorCache, SVulkan::FGfxPSO* PSO, SVulkan::FCmdBuffer* CmdBuffer)
{
	FMarkerScope MarkerScope(Device, CmdBuffer, "JOTL");

	FStagingBuffer* IB = StagingMgr.AcquireBuffer(24 * sizeof(uint16_t), CmdBuffer);
	uint16_t* IBData = (uint16_t*)IB->Buffer->Lock();
	*IBData++ = 0; *IBData++ = 1; *IBData++ = 2;
	*IBData++ = 1; *IBData++ = 3; *IBData++ = 2;
	*IBData++ = 2; *IBData++ = 3; *IBData++ = 4;
	*IBData++ = 3; *IBData++ = 5; *IBData++ = 4;
	IB->Buffer->Unlock();

	struct FUIVertex : ImDrawVert
	{
		FUIVertex(float x, float y, float u, float v, uint32 c)
		{
			pos.x = x;
			pos.y = y;
			uv.x = u;
			uv.y = v;
			col = c;
		}
	};

	const float Third = 1.0f / 3.0f;

	const float HexWidth = 1.1547f;
	const float HexSideLength = 0.5774f;
	const float HexHeight = HexWidth;//1.0f;

	FStagingBuffer* VB = StagingMgr.AcquireBuffer(sizeof(ImDrawVert) * 6, CmdBuffer);
	ImDrawVert* VBData = (ImDrawVert*)VB->Buffer->Lock();
	*VBData++ = FUIVertex(0,													HexHeight * 0.5,	0, 0, 0xffffffff);
	*VBData++ = FUIVertex((HexWidth - HexSideLength) * 0.5f,					0,					0, 0, 0xffffffff);
	*VBData++ = FUIVertex((HexWidth - HexSideLength) * 0.5f,					1,					0, 0, 0xffffffff);
	*VBData++ = FUIVertex((HexWidth - HexSideLength) * 0.5f + HexSideLength,	0,					0, 0, 0xffffffff);
	*VBData++ = FUIVertex((HexWidth - HexSideLength) * 0.5f + HexSideLength,	1,					0, 0, 0xffffffff);
	*VBData++ = FUIVertex(HexWidth,												HexHeight * 0.5,	0, 0, 0xffffffff);
	VB->Buffer->Unlock();

	struct FUICB
	{
		FVector2 Scale;
		FVector2 Translation;
		FVector4 Color;
	};

	vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, IB->Buffer->Buffer.Buffer, 0, VK_INDEX_TYPE_UINT16);
	VkDeviceSize Zero = 0;
	vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &VB->Buffer->Buffer.Buffer, &Zero);

	auto DrawHex = [&](float Scale, FVector2 Translation, FVector4 Color)
	{
		FStagingBuffer* CB = StagingMgr.AcquireBuffer(sizeof(float) * 4, CmdBuffer);
		FUICB* CBData = (FUICB*)CB->Buffer->Lock();
		CBData->Scale.Set(Scale, Scale);
		CBData->Translation = Translation;
		CBData->Color = Color;
		CB->Buffer->Unlock();

		FDescriptorPSOCache Cache(PSO);
		Cache.SetUniformBuffer("CB", *CB->Buffer);
		Cache.UpdateDescriptors(DescriptorCache, CmdBuffer);

		vkCmdDrawIndexed(CmdBuffer->CmdBuffer, 24, 1, 0, 0, 0);
	};

	const float Scale = 0.1f;

	auto GetTranslation = [&](int Row, int Col)
	{
		float x = Scale * ((float)Col * ((HexWidth - HexSideLength) * 0.5f + HexSideLength));
		float y = (float)Row * Scale * HexHeight;
		if (Col % 2)
		{
			y += 0.5f * HexHeight * Scale;
		}
		return FVector2(x, y);
	};

	auto GetScaledTranslation = [&](int Row, int Col, float InScale)
	{
		float x = Scale * ((float)Col * ((HexWidth - HexSideLength) * 0.5f + HexSideLength));
		x += InScale * Scale * HexSideLength;
		float y = (float)Row * Scale * HexHeight;
		if (Col % 2)
		{
			y += 0.5f * HexHeight * Scale;
		}
		y += InScale * Scale * HexHeight * 0.5f;
		return FVector2(x, y);
	};

	for (int Row = 0; Row < Game.Scenario->NumRows; ++Row)
	{
		for (int Col = 0; Col < Game.Scenario->NumCols; ++Col)
		{
			FVector2 Translation = GetTranslation(Row, Col);
			FVector4 Color = FVector4::GetZero();
			char Hex = Game.Scenario->Map[Row][Col];
			switch (Hex)
			{
			case ' ': continue;
			//case 'X': Color.Set(0, 0, 0.7f, 1); break;
			case 'O': Color.Set(0, 0.7f, 0, 1); break;
			//case 'E': Color.Set(0.7f, 0.7f, 0, 1); break;
			case '.': Color.Set(0.6f, 0.8f, 0.8f, 1); break;
			//case 'N': Color.Set(0.3f, 0.3f, 0.3f, 1); break;
			default:
				check(0);
			}

			DrawHex(Scale, Translation, Color);
		}
	}

	for (FCharacterInstance* CI : Game.Scenario->CharacterInstances)
	{
		if (!CI->bExhausted)
		{
			FVector2 Translation = GetScaledTranslation(CI->Col, CI->Row, 0.5f);
			DrawHex(Scale * 0.5f, Translation, FVector4(0, 0, 1, 1));
		}
	}

	for (auto& Pair : Game.Scenario->MonsterInstances)
	{
		for (FMonsterInstance* MonsterInstance : Pair.second)
		{
			FVector2 Translation = GetScaledTranslation(MonsterInstance->Col, MonsterInstance->Row, 0.5f);
			if (MonsterInstance->bElite)
			{
				DrawHex(Scale * 0.5f, Translation, FVector4(0.85f, 0.85f, 0, 1));
			}
			else
			{
				DrawHex(Scale * 0.5f, Translation, FVector4(0.3f, 0.3f, 0.3f, 1));
			}
		}
	}

	//DrawHex(Scale, FVector2(0.0f, 0.0f), FVector4(1, 0, 0, 1));
	//DrawHex(Scale, FVector2(2.0f * Scale * Third, 0.5f * Scale), FVector4(1, 1, 0, 1));
	//DrawHex(Scale, FVector2(0.0f, Scale), FVector4(1, 0, 1, 1));
}


#if 0
int main()
{
	int MaxRounds = 0;
	int Wins = 0;
	int Losses = 0;
	while (1)
	{
		while (!Game.IsScenarioFinishedOrAllCharactersExhausted())
		{
			Game.PlayRound();
		}

		if (Game.Scenario.AreAllCharactersExhausted())
		{
			std::cout << "***** YOU LOSE *****" << std::endl;
			++Losses;
		}
		else
		{
			std::cout << "***** YOU WIN! *****" << std::endl;
			++Wins;
		}

		MaxRounds = Max(Game.Scenario.Round, MaxRounds);
	}
}
#endif
