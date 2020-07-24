
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

template <typename T>
inline void ShuffleDeck(std::vector<T>& Deck)
{
	std::vector<T> NewDeck;
	while (!Deck.empty())
	{
		auto Index = rand() % Deck.size();
		NewDeck.push_back(Deck[Index]);
		Deck.erase(Deck.begin() + Index);
	}

	Deck.swap(NewDeck);
}

struct FAbilityCard
{
	int Initiative = -1;

	std::string Name;

	FAbilityCard(int InInitative, std::string InName)
	{
		Initiative = InInitative;
		Name = InName;
	}
};

struct FBasicAbilityCard : FAbilityCard
{
	FBasicAbilityCard(int InInitative)
		: FAbilityCard(InInitative, "Basic Melee Attack 2")
	{
	}
};

struct FModifier
{
	enum EType
	{
		MissShuffle,
		DoubleShuffle,
		Damage,
	};
	EType Type;
	int Delta;

	FModifier() = default;
	FModifier(EType InType, int InDelta)
		: Type(InType)
		, Delta(InDelta)
	{
	}
};

struct FModifierDeck
{
	std::vector<FModifier> Modifiers;
	std::vector<FModifier> RemainingModifiers;

	void Shuffle()
	{
		RemainingModifiers = Modifiers;
		ShuffleDeck(RemainingModifiers);
	}

	FModifier GetNext()
	{
		if (RemainingModifiers.empty())
		{
			Shuffle();
		}

		FModifier M = RemainingModifiers.back();
		RemainingModifiers.resize(RemainingModifiers.size() - 1);
		return M;
	}
};

struct FMonsterCard
{
	int Initiative = -1;

	std::string Name;

	FMonsterCard(int InInitative, std::string InName)
	{
		Initiative = InInitative;
		Name = InName;
	}
};

struct FAbilityCardPair
{
	FAbilityCard* Top = nullptr;
	FAbilityCard* Bottom = nullptr;
	int SelectedInitative = -1;

	void Reset()
	{
		Top = nullptr;
		Bottom = nullptr;
		SelectedInitative = -1;
	}

	int GetUnselectedInitiative()
	{
		if (SelectedInitative == Top->Initiative)
		{
			return Bottom->Initiative;
		}

		return Top->Initiative;
	}
};

enum ECharacter
{
	EDemolitionist,
	ERedGuard,
	EHatched,
	EVoidWarden,
};

struct FCharacter
{
	std::vector<FAbilityCard*> Cards;

	ECharacter Type;
	std::string Name;
	int HP = 0;
	bool bExhausted = false;
	FModifierDeck Modifiers;

	FCharacter(ECharacter InType, std::string InName, int InHP)
	{
		Type = InType;
		Name = InName;
		HP = InHP;
	}

	void Setup()
	{
		Available = Cards;
		Hand.Reset();
		Lost.clear();
		Discarded.clear();
		bExhausted = false;
		Modifiers.Shuffle();
	}

	FModifier GetModifier()
	{
		return Modifiers.GetNext();
	}

	bool IsExhausted()
	{
		return bExhausted;
	}

	bool SelectHand()
	{
		std::cout << "\t\t" << Available.size() << " cards available" << std::endl;

		if (Available.size() < 2)
		{
			std::cout << "\t\tSHORT REST NEEDED!" << std::endl;
			for (FAbilityCard* Card : Discarded)
			{
				Available.push_back(Card);
			}
			Discarded.clear();
			ShuffleDeck(Available);
			FAbilityCard* LostCard = Available.back();
			Lost.push_back(LostCard);
			Available.resize(Available.size() - 1);

			std::cout << "\t\t" << Available.size() << " cards remaining" << std::endl;
		}

		if (Available.size() < 2)
		{
			std::cout << "\t\t\tEXHAUSTED!" << std::endl;
			bExhausted = true;
			return false;
		}

		// TEMP
		{
			ShuffleDeck(Available);
		}
		auto LastIndex = Available.size() - 1;
		Hand.Top = Available[LastIndex];
		Hand.Bottom = Available[LastIndex - 1];
		Available.resize(LastIndex - 1);
		Hand.SelectedInitative = (rand() % 2) == 0 ? Hand.Top->Initiative : Hand.Bottom->Initiative;

		std::cout << "\t\t\t Top " << Hand.Top->Initiative << " Bottom " << Hand.Bottom->Initiative << ", USING " << Hand.SelectedInitative << std::endl;
		return true;
	}

	void EndRound()
	{
		Discarded.push_back(Hand.Top);
		Discarded.push_back(Hand.Bottom);
		Hand.Reset();
	}

	std::vector<FAbilityCard*> Available;
	FAbilityCardPair Hand;
	std::vector<FAbilityCard*> Lost;
	std::vector<FAbilityCard*> Discarded;
};

struct FDemolitionist : FCharacter
{
	FDemolitionist()
		: FCharacter(EDemolitionist, "Demo Man", 9)
	{
		Cards.push_back(new FBasicAbilityCard(19));
		Cards.push_back(new FBasicAbilityCard(32));
		Cards.push_back(new FBasicAbilityCard(33));
		Cards.push_back(new FBasicAbilityCard(66));
		Cards.push_back(new FBasicAbilityCard(67));
		Cards.push_back(new FBasicAbilityCard(72));
		Cards.push_back(new FBasicAbilityCard(74));
		Cards.push_back(new FBasicAbilityCard(79));
		Cards.push_back(new FBasicAbilityCard(88));

		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 2));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -2));
		Modifiers.Modifiers.push_back(FModifier(FModifier::DoubleShuffle, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::MissShuffle, 0));
	}
} GDemolitionist;

struct FRedGuard : FCharacter
{
	FRedGuard()
		: FCharacter(ERedGuard, "Red Guard", 11)
	{
		Cards.push_back(new FBasicAbilityCard(9));
		Cards.push_back(new FBasicAbilityCard(10));
		Cards.push_back(new FBasicAbilityCard(11));
		Cards.push_back(new FBasicAbilityCard(12));
		Cards.push_back(new FBasicAbilityCard(18));
		Cards.push_back(new FBasicAbilityCard(19));
		Cards.push_back(new FBasicAbilityCard(22));
		Cards.push_back(new FBasicAbilityCard(32));
		Cards.push_back(new FBasicAbilityCard(40));
		Cards.push_back(new FBasicAbilityCard(60));

		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 2));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		Modifiers.Modifiers.push_back(FModifier(FModifier::Damage, -2));
		Modifiers.Modifiers.push_back(FModifier(FModifier::DoubleShuffle, 0));
		Modifiers.Modifiers.push_back(FModifier(FModifier::MissShuffle, 0));
	}
} GRedGuard;

struct FMonster
{
	std::vector<FMonsterCard*> Cards;

	std::string Name;

	struct FStats
	{
		int HP;
		int Move;
		int Damage;
	};

	FStats Normal;
	FStats Elite;

	FMonster(std::string InName, 
		int InHP, int InMove, int InDamage,
		int InHPElite, int InMoveElite, int InDamageElite)
	{
		Name = InName;

		Normal.HP = InHP;
		Normal.Move = InMove;
		Normal.Damage = InDamage;

		Elite.HP = InHPElite;
		Elite.Move = InMoveElite;
		Elite.Damage = InDamageElite;

		Cards.push_back(new FMonsterCard(50, "Basic Attack"));
	}

	void Setup()
	{
		UsedCards.clear();
		UnusedCards = Cards;
		ShuffleDeck(UnusedCards);
		ActiveCard = nullptr;
	}

	int GetStartHP(bool bElite)
	{
		return bElite ? Elite.HP : Normal.HP;
	}

	int GetBaseDamage(bool bElite)
	{
		return bElite ? Elite.Damage : Normal.Damage;
	}

	int GetBaseMove(bool bElite)
	{
		return bElite ? Elite.Move : Normal.Move;
	}

	FMonsterCard* GetNextCard()
	{
		if (UnusedCards.empty())
		{
			UsedCards.swap(UnusedCards);
		}

		ActiveCard = UnusedCards.back();
		UnusedCards.resize(UnusedCards.size() - 1);
		UsedCards.push_back(ActiveCard);

		return ActiveCard;
	}

	FMonsterCard* ActiveCard = nullptr;
	std::vector<FMonsterCard*> UnusedCards;
	std::vector<FMonsterCard*> UsedCards;
};

struct FVermlingRaider : public FMonster
{
	FVermlingRaider()
		: FMonster("Vermling Raider", 
			5, 1, 2,
			10, 1, 2)
	{
		Cards.push_back(new FMonsterCard(50, "Nothing Special"));
	}
} GVermlingRaider;

struct FMonsterInstance
{
	FMonster* Monster;
	int MonsterIndex;
	bool bElite;
	int HP;
	std::string Name;

	FMonsterInstance(FMonster* InMonster, int InMonsterIndex, bool bInElite)
	{
		Monster = InMonster;
		MonsterIndex = InMonsterIndex;
		bElite = bInElite;
		HP = Monster->GetStartHP(bElite);

		std::stringstream ss;
		if (bElite)
		{
			ss << "Elite ";
		}
		ss << Monster->Name << " " << MonsterIndex;
		ss.flush();
		Name = ss.str();
	}

	std::string GetName()
	{
		return Name;
	}
};

struct FTurn
{
	int Initiative = 0;

	FCharacter* Character = nullptr;
	FAbilityCardPair* CharacterCards = nullptr;

	FMonster* Monster = nullptr;
	FMonsterCard* MonsterCard = nullptr;

	FTurn(FCharacter* InCharacter, FAbilityCardPair* InCharacterCards)
	{
		Character = InCharacter;
		CharacterCards = InCharacterCards;
		Initiative = CharacterCards->SelectedInitative;
	}

	FTurn(FMonster* InMonster, FMonsterCard* InMonsterCard)
	{
		Monster = InMonster;
		MonsterCard = InMonsterCard;
		Initiative = MonsterCard->Initiative;
	}
};

struct FScenario
{
	int Round = 0;
	std::vector<FCharacter*> Characters;
	std::map<FMonster*, std::vector<FMonsterInstance*>> MonsterInstances;
	std::vector<FMonster*> Monsters;

	FScenario()
	{
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 2));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, -1));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::Damage, -2));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::DoubleShuffle, 0));
		MonsterModifiers.Modifiers.push_back(FModifier(FModifier::MissShuffle, 0));
	}

	void Setup(std::vector<FCharacter*>& InCharacters)
	{
		Characters = InCharacters;
		for (FCharacter* C : Characters)
		{
			C->Setup();
		}

		AddAndSetupMonsters();
	}

	void AddAndSetupMonsters()
	{
		Monsters.push_back(&GVermlingRaider);
		MonsterInstances[&GVermlingRaider].push_back(new FMonsterInstance(&GVermlingRaider, 1, false));
		MonsterInstances[&GVermlingRaider].push_back(new FMonsterInstance(&GVermlingRaider, 3, false));
		MonsterInstances[&GVermlingRaider].push_back(new FMonsterInstance(&GVermlingRaider, 6, true));

		// Set Elite first
		for (auto& Pair : MonsterInstances)
		{
			std::vector<FMonsterInstance*>& Instances = Pair.second;
			std::qsort(Instances.data(), Instances.size(), sizeof(Instances[0]), 
				[](const void* InA, const void* InB)
				{
					FMonsterInstance* A = *(FMonsterInstance**)InA;
					FMonsterInstance* B = *(FMonsterInstance**)InB;
					if (A->bElite && !B->bElite)
					{
						return -1;
					}
					else if (!A->bElite && B->bElite)
					{
						return 1;
					}

					return A->MonsterIndex < B->MonsterIndex ? -1 : 1;
				});
		}

		for (FMonster* M : Monsters)
		{
			M->Setup();
		}
	}

	bool IsFinished()
	{
		return MonsterInstances.empty();
	}

	bool PlayCharacterCards()
	{
		std::cout << "*** Player Cards" << std::endl;

		int NotExhausted = 0;
		for (FCharacter* C : Characters)
		{
			if (!C->IsExhausted())
			{
				std::cout << "\t" << C->Name << std::endl;
				if (C->SelectHand())
				{
					Turns.push_back(FTurn(C, &C->Hand));
					++NotExhausted;
				}
			}
		}

		if (NotExhausted == 0)
		{
			std::cout << "All characters exhausted!" << std::endl;
			return false;
		}

		return true;
	}

	void PrintInfo()
	{
		ImGui::Begin("Game Info");
		{
			char s[64];
			sprintf(s, "Round %d", Round);
			ImGui::Text(s);
		}

		//std::cout << "***** ROUND " << Round << "*****" << std::endl;
		ImGui::Text("*** Characters ***");
		//std::cout << "\t*** Characters" << std::endl;
		for (FCharacter* C : Characters)
		{
			char s[64];
			//std::cout << "\t\t" << C->Name << " ";
			if (C->IsExhausted())
			{
				sprintf(s, "%s EXHAUSTED!", C->Name.c_str());
			}
			else
			{
				sprintf(s, "%s HP %d!", C->Name.c_str(), C->HP);
			}
			ImGui::Text(s);
		}

		ImGui::Text("*** Monsters ***");
		//std::cout << "\t*** Monsters" << std::endl;
		for (FMonster* Monster : Monsters)
		{
			for (FMonsterInstance* Instance : MonsterInstances[Monster])
			{
				//std::cout << "\t\t" << Instance->Name << " " << "HP " << Instance->HP << std::endl;
				char s[64];
				sprintf(s, "%s HP %d!", Instance->Name.c_str(), Instance->HP);
				ImGui::Text(s);
			}
		}
	}

	void PlayRound()
	{
		++Round;
		Turns.clear();
		if (PlayCharacterCards())
		{
			SelectMonsterCards();
			OrderInitiative();
			PlayTurns();
			ChekEndOfRound();
		}
	}

	void SelectMonsterCards()
	{
		std::cout << "*** Monsters Cards" << std::endl;
		for (FMonster* M : Monsters)
		{
			std::cout << "\t" << M->Name << std::endl;
			FMonsterCard* Card = M->GetNextCard();
			std::cout << "\t\t" << Card->Initiative << " " << Card->Name << std::endl;
			Turns.push_back(FTurn(M, Card));
		}
	}

	void OrderInitiative()
	{
		std::qsort(Turns.data(), Turns.size(), sizeof(Turns[0]), 
			[](const void* InA, const void* InB) -> int
			{
				FTurn* A = (FTurn*)InA;
				FTurn* B = (FTurn*)InB;

				if (A->Initiative < B->Initiative)
				{
					return -1;
				}
				else if (A->Initiative > B->Initiative)
				{
					return 1;
				}

				if (A->Character && !B->Character)
				{
					return -1;
				}
				else if (!A->Character && B->Character)
				{
					return 1;
				}
				else if (A->Character && B->Character)
				{
					int IA = A->CharacterCards->GetUnselectedInitiative();
					int IB = B->CharacterCards->GetUnselectedInitiative();
					if (IA < IB)
					{
						return -1;
					}

					return IA == IB ? 0 : 1;
				}

				__debugbreak();
				return -1;
			});

		std::cout << "*** Initiative Order" << std::endl;

		for (FTurn& Turn : Turns)
		{
			std::cout << "\t" << Turn.Initiative << " ";
			if (Turn.Character)
			{
				std::cout << Turn.Character->Name;
			}
			else
			{
				std::cout << Turn.Monster->Name;
			}
			std::cout << std::endl;
		}
	}

	void PlayTurns()
	{
		std::cout << "*** Turns!" << std::endl;
		for (FTurn& Turn : Turns)
		{
			if (Turn.Character)
			{
				if (!Turn.Character->bExhausted)
				{
					std::cout << "\t** " << Turn.Character->Name << "'s Turn" << std::endl;
					PlayCharacterTurn(Turn);
				}
			}
			else
			{
				if (std::find(Monsters.begin(), Monsters.end(), Turn.Monster) != Monsters.end())
				{
					for (FMonsterInstance* Instance : MonsterInstances[Turn.Monster])
					{
						std::cout << "\t** " << Instance->GetName() << " 's Turn" << std::endl;
						PlayMonsterTurn(Turn, Instance);
					}
				}
			}
		}
	}

	FMonsterInstance* FindClosestMonsterInstance()
	{
		auto NumMonsterTypes = MonsterInstances.size();
		if (NumMonsterTypes == 0)
		{
			return nullptr;
		}

		auto MonsterTypeIndex = rand() % NumMonsterTypes;
		auto Iterator = MonsterInstances.begin();
		while (MonsterTypeIndex > 0)
		{
			++Iterator;
			--MonsterTypeIndex;
		}

		auto& Instances = Iterator->second;
		auto InstanceIndex = rand() % Instances.size();
		return Instances[InstanceIndex];
	}


	void KillMonster(FMonsterInstance* Instance)
	{
		auto& Instances = MonsterInstances[Instance->Monster];
		auto Iterator = Instances.begin();
		for (size_t Index = 0; Index < Instances.size(); Index++)
		{
			if (Instances[Index] == Instance)
			{
				break;
			}
			++Iterator;
		}
		Instances.erase(Iterator);
		if (Instances.empty())
		{
			MonsterInstances.erase(Instance->Monster);
			Monsters.erase(std::find(Monsters.begin(), Monsters.end(), Instance->Monster));
		}
	}

	void AttackMonster(FCharacter* Character, FModifier Modifier, FMonsterInstance* Instance)
	{
		int Damage = 2;
		std::cout << "\t\t\tAttack " << Damage << " ";
		if (Modifier.Type == FModifier::MissShuffle)
		{
			std::cout << " to " << Instance->GetName() << "... MISS!" << std::endl;
			Character->Modifiers.Shuffle();
			return;
		}
		else if (Modifier.Type == FModifier::DoubleShuffle)
		{
			std::cout << Damage << "... 2X!";
			Damage *= 2;
			Character->Modifiers.Shuffle();
		}
		else
		{
			std::cout << " + ";
			if (Modifier.Delta >= 0)
			{
				std::cout << "+";
			}
			std::cout << Modifier.Delta;
			Damage += Modifier.Delta;
		}

		Instance->HP = Max(Instance->HP - Damage, 0);

		std::cout << " to " << Instance->GetName() << ", final HP " << Instance->HP;
		if (Instance->HP == 0)
		{
			std::cout << " DEAD!";
			KillMonster(Instance);
		}
		std::cout << std::endl;
	}

	void AttackCharacter(FMonsterInstance* Instance, FModifier Modifier, FCharacter* Character)
	{
		int Damage = Instance->Monster->GetBaseDamage(Instance->bElite);
		std::cout << "\t\t\tAttack " << Damage << " ";
		if (Modifier.Type == FModifier::MissShuffle)
		{
			std::cout << " to " << Character->Name << "... MISS!" << std::endl;
			MonsterModifiers.Shuffle();
			return;
		}
		else if (Modifier.Type == FModifier::DoubleShuffle)
		{
			std::cout << Damage << "... 2X!";
			Damage *= 2;
			MonsterModifiers.Shuffle();
		}
		else
		{
			std::cout << " + ";
			if (Modifier.Delta >= 0)
			{
				std::cout << "+";
			}
			std::cout << Modifier.Delta;
			Damage += Modifier.Delta;
		}

		Character->HP = Max(Character->HP - Damage, 0);

		std::cout << " to " << Character->Name << ", final HP " << Character->HP;
		if (Character->HP == 0)
		{
			std::cout << " EXHAUSTED!";
			Character->bExhausted = true;
		}
		std::cout << std::endl;
	}

	void PlayCharacterAbility(FCharacter* Character, FAbilityCard* Card)
	{
		std::cout << "\t\t" << Card->Name << std::endl;
		FMonsterInstance* Instance = FindClosestMonsterInstance();
		if (Instance)
		{
			FModifier Modifier = Character->GetModifier();
			AttackMonster(Character, Modifier, Instance);
		}
		else
		{
			std::cout << "\t\t\tPASS..." << std::endl;
		}
	}

	void PlayCharacterTurn(FTurn& Turn)
	{
		PlayCharacterAbility(Turn.Character, Turn.CharacterCards->Top);
		PlayCharacterAbility(Turn.Character, Turn.CharacterCards->Bottom);
	}

	FCharacter* FindClosestCharacter()
	{
		if (!AreAllCharactersExhausted())
		{
			while (true)
			{
				auto Index = rand() % Characters.size();
				if (!Characters[Index]->bExhausted)
				{
					return Characters[Index];
				}
			}
		}

		return nullptr;
	}

	void PlayMonsterTurn(FTurn& Turn, FMonsterInstance* Instance)
	{
		FCharacter* Character = FindClosestCharacter();
		if (Character && !Character->bExhausted)
		{
			FModifier Modifier = MonsterModifiers.GetNext();
			AttackCharacter(Instance, Modifier, Character);
		}
	}

	void ChekEndOfRound()
	{
		for (FCharacter* C : Characters)
		{
			C->EndRound();
		}
	}

	bool AreAllCharactersExhausted()
	{
		for (FCharacter* C : Characters)
		{
			if (!C->IsExhausted())
			{
				return false;
			}
		}
		return true;
	}

	std::vector<FTurn> Turns;
	FModifierDeck MonsterModifiers;
};

struct FGame
{
	FScenario Scenario;

	FGame()
	{
		srand((unsigned int)time(NULL));
		AddCharacter(&GDemolitionist);
		AddCharacter(&GRedGuard);
		SetupScenario();
	}

	void SetupScenario()
	{
		Scenario.Setup(Characters);
	}

	bool IsScenarioFinishedOrAllCharactersExhausted()
	{
		return Scenario.IsFinished() || Scenario.AreAllCharactersExhausted();
	}

	float RoundTimer = 0;
	void PlayRound(float DeltaTime)
	{
		RoundTimer += DeltaTime;

		Scenario.PrintInfo();
		const float TTL = 2;
		if (RoundTimer > TTL)
		{
			Scenario.PlayRound();
			RoundTimer = 0;
		}
	}

	void AddCharacter(FCharacter* InCharacter)
	{
		Characters.push_back(InCharacter);
	}

	std::vector<FCharacter*> Characters;
};


void Tick(float DeltaTime)
{
	static FGame Game;

	if (!Game.IsScenarioFinishedOrAllCharactersExhausted())
	{
		Game.PlayRound(DeltaTime);
	}
	else
	{
		ImGui::Begin("Game Over");
		if (Game.Scenario.AreAllCharactersExhausted())
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

	const char* Map[] =
	{
		"       ..OE",
		"       .O..",
		"XX....N.O..",
		"X..N...O...",
		"X O ...O...",
		"     ..EN.N",
		"      . . .",
	};

	const int NumRows = 7;
	const int NumCols = 11;
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

	for (int Row = 0; Row < NumRows; ++Row)
	{
		for (int Col = 0; Col < NumCols; ++Col)
		{
			FVector2 Translation = GetTranslation(Row, Col);
			FVector4 Color = FVector4::GetZero();
			char Hex = Map[Row][Col];
			switch (Hex)
			{
			case ' ': continue;
			case 'X': Color.Set(0, 0, 0.7f, 1); break;
			case 'O': Color.Set(0, 0.7f, 0, 1); break;
			case 'E': Color.Set(0.7f, 0.7f, 0, 1); break;
			case '.': Color.Set(1, 1, 1, 1); break;
			case 'N': Color.Set(0.3f, 0.3f, 0.3f, 1); break;
			default:
				check(0);
			}

			DrawHex(Scale, Translation, Color);
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
