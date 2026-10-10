#include "Inkpot/InkpotStory.h"
#include "Inkpot/InkpotChoice.h"
#include "Inkpot/InkpotValueLibrary.h"
#include "Inkpot/InkpotListLibrary.h"
#include "Inkpot/Inkpot.h"
#include "Ink/Choice.h"
#include "Ink/StoryState.h"
#include "Ink/SearchResult.h"
#include "Ink/Path.h"
#include "Ink/InkList.h"
#include "Utility/InkpotLog.h"
#include "Inkpot/InkpotGameplayTagLibrary.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingRegistry.h"
#include "Settings/InkpotSettings.h"
#include "Ink/Container.h"
#include "Ink/Divert.h"
#include "GameplayTagsManager.h"
#include "Logging/MessageLog.h"
#include "Engine/Engine.h"

void UInkpotStory::Initialise( TSharedPtr<FInkpotStoryInternal>  InInkpotStory )
{
	StoryInternal = InInkpotStory;
	StoryInternal->OnDidContinue().AddUObject(this, &UInkpotStory::OnContinueInternal );
	StoryInternal->OnMakeChoice().AddUObject(this, &UInkpotStory::OnMakeChoiceInternal );
	StoryInternal->OnEvaluateFunction().AddUObject(this, &UInkpotStory::OnEvaluateFunctionInternal );
	StoryInternal->OnCompleteEvaluateFunction().AddUObject(this, &UInkpotStory::OnCompleteEvaluateFunctionInternal );
	StoryInternal->OnChoosePathString().AddUObject(this, &UInkpotStory::OnChoosePathStringInternal );
	BindOnVariableStateChangeEvent();
	BindOnStoryErrorEvent();
	bExternalBindingsValidated = false;
}

TSharedPtr<FInkpotStoryInternal> UInkpotStory::GetStoryInternal()
{
	return StoryInternal;
}

int32 UInkpotStory::GetID() const
{
	if(!StoryInternal.IsValid())
		return FInkpotStoryInternal::BadStoryHandle;
	return StoryInternal->GetID();
}

bool UInkpotStory::IsValid() const
{
	if(!StoryInternal.IsValid())
		return false;
	return StoryInternal->IsValidStory();
}

FString UInkpotStory::GetCurrentText()
{
	FString currentText = StoryInternal->GetCurrentText();
	currentText.TrimEndInline();
	return currentText;
}

const TArray<FString>& UInkpotStory::GetCurrentTags()
{
	return StoryInternal->GetCurrentTags();
}

TArray<FString> UInkpotStory::GlobalTags()
{
	return StoryInternal->GlobalTags();
}

TArray<FString> UInkpotStory::TagsForContentAtPath( const FString &InPath)
{
	return StoryInternal->TagsForContentAtPath( InPath );
}

TArray<FString> UInkpotStory::TagsForContentAtPathGT(FGameplayTag InPath )
{
	return TagsForContentAtPath( UInkpotGameplayTagLibrary::PathTagToString(InPath) );
}

FGameplayTag UInkpotStory::GetGTFromString(const FString InTagString, const EInkGameplayTagTypes TagType, bool& FoundTag)
{
	FString TagString;
	switch(TagType)
	{
	case EInkGameplayTagTypes::INK_ORIGIN:
		TagString = INK_ORIGIN_GAMEPLAYTAG_PREFIX + InTagString;
		break;
	case EInkGameplayTagTypes::INK_PATH:
		TagString = INK_PATH_GAMEPLAYTAG_PREFIX + InTagString;
		break;
	case EInkGameplayTagTypes::INK_VAR:
		TagString = INK_VARIABLE_GAMEPLAYTAG_PREFIX + InTagString;
		break;
	}

	FName TagName = FName(*TagString);
	FGameplayTag OutTag = UGameplayTagsManager::Get().RequestGameplayTag(TagName, false);
	if (OutTag.IsValid())
	{
		FoundTag = true;
	}
	else
	{
		FoundTag = false;
	}
	return OutTag;
}

void UInkpotStory::ChoosePath( const FString &InPath )
{
	ChoosePathInternal(InPath);
}

void UInkpotStory::ChoosePathGT(FGameplayTag InPath )
{
	ChoosePath( UInkpotGameplayTagLibrary::PathTagToString(InPath) );
}

void UInkpotStory::ChoosePathInternal( const FString &InPath )
{
	FActiveStoryScope scope(this);
	StoryInternal->ChoosePathString( InPath, true, TArray<TSharedPtr<Ink::FValueType>>() );
}

void UInkpotStory::ChoosePathString( const FString &InPath, const TArray<FInkpotValue> &InValues )
{
	ChoosePathStringInternal( InPath, InValues );
}

void UInkpotStory::ChoosePathStringGT(FGameplayTag InPath, const TArray<FInkpotValue> &InValues )
{
	ChoosePathString( UInkpotGameplayTagLibrary::PathTagToString(InPath), InValues );
}

int UInkpotStory::VisitCountAtPathString( const FString &Path )
{
	int count = StoryInternal->GetStoryState()->VisitCountAtPathString(Path);
	return count;
}

int UInkpotStory::VisitCountAtPathStringGT(FGameplayTag InPath )
{
	return VisitCountAtPathString( UInkpotGameplayTagLibrary::PathTagToString(InPath) );
}

TSharedPtr<Ink::FListDefinition> UInkpotStory::GetListOrigin(const FString& InOriginName, const FString& InItemName)
{
	TSharedPtr<Ink::FListDefinitionsOrigin> definitions = StoryInternal->GetListDefinitions();
	TSharedPtr<Ink::FListDefinition> origin;
	bool gotDefintion = definitions->TryListGetDefinition(InOriginName, origin);
	if (!gotDefintion)
	{
		INKPOT_ERROR("Failed to find List definition '%s'", *InOriginName);
		return nullptr;
	}

	if(InItemName.Len())
	{
		bool gotItem = origin->ContainsItemWithName(InItemName);
		if (!gotItem)
		{
			INKPOT_ERROR("Failed to find entry '%s' in List definition '%s'", *InItemName, *InOriginName);
			return nullptr;
		}
	}
	return origin;
}

bool UInkpotStory::CreateInkValues( const TArray<FInkpotValue>& InValues, TArray<TSharedPtr<Ink::FValueType>> &OutValues)
{
	OutValues.Reserve( InValues.Num() );
	for( const FInkpotValue &inValue : InValues )
	{
		if(!inValue.IsValid())
		{
			INKPOT_ERROR("Bad value being passed to Ink");
			return false;
		}
		else
		{
			if ( (*inValue)->HasSubtype<Ink::FInkList>() )
			{
				FInkpotList list = UInkpotValueLibrary::InkpotValueAsList( inValue );
				if(!list.ValidateOrigin(this))
					return false;
			}
			OutValues.Emplace( *inValue );
		}
	}
	return true;
}

void UInkpotStory::ChoosePathStringInternal( const FString& InPath, const TArray<FInkpotValue>& InValues )
{
	TArray<TSharedPtr<Ink::FValueType>> values;
	if(!CreateInkValues( InValues, values ))
		return;
	StoryInternal->ChoosePathString( InPath, true, values );
}

FString UInkpotStory::Continue()
{
	return ContinueInternal();
}

FString UInkpotStory::ContinueIfYouCan(bool& Continued)
{
	if (CanContinue())
	{
		Continued = true;
		return ContinueInternal();
	}
	else
	{
		Continued = false;
		return FString();
	}
}

FString UInkpotStory::ContinueInternal()
{
	if (IsLineRenderingForFlow(GetCurrentFlowName()))
	{
		INKPOT_WARN("Continue ignored while a line is rendering for flow '%s'", *GetCurrentFlowName());
		return GetCurrentText();
	}
	EnsureExternalBindingsValidated();
	FActiveStoryScope scope(this);
	return StoryInternal->Continue();
}

FString UInkpotStory::ContinueMaximally()
{
	return ContinueMaximallyInternal();
}

FString UInkpotStory::ContinueMaximallyInternal()
{
	if (IsLineRenderingForFlow(GetCurrentFlowName()))
	{
		INKPOT_WARN("ContinueMaximally ignored while a line is rendering for flow '%s'", *GetCurrentFlowName());
		return GetCurrentText();
	}
	EnsureExternalBindingsValidated();
	FActiveStoryScope scope(this);
	return StoryInternal->ContinueMaximally();
}

FString UInkpotStory::ContinueMaximallyAtPath(const FString& InPath)
{
	FString ret; 
	ChoosePath(InPath);
	if (CanContinue())
		ret = ContinueMaximally();
	return ret;
}

FString UInkpotStory::ContinueMaximallyAtPathGT(FGameplayTag InPath )
{
	return ContinueMaximallyAtPath( UInkpotGameplayTagLibrary::PathTagToString(InPath) );
}

bool UInkpotStory::CanContinue()
{
	return CanContinueInternal();
}

bool UInkpotStory::CanContinueInternal()
{
	if (IsLineRenderingForFlow(GetCurrentFlowName()))
		return false;
	return StoryInternal->CanContinue();
}

bool UInkpotStory::HasChoices()
{
	return StoryInternal->HasChoices();
}

const TArray<UInkpotChoice*>& UInkpotStory::GetCurrentChoices()
{
	return Choices;
}

void UInkpotStory::UpdateChoices()
{
	Choices.Empty();

	if(!StoryInternal->HasChoices())
		return;

	TArray<TSharedPtr<Ink::FChoice>> inkChoices =  StoryInternal->GetCurrentChoices();
	for( auto inkChoice : inkChoices  )
	{
		UInkpotChoice* choice = NewObject<UInkpotChoice>( this );
		choice->Initialise( inkChoice );
		Choices.Emplace( choice );
	}
}

void UInkpotStory::ChooseChoice( UInkpotChoice* InChoice )
{
	if(!InChoice)
		return;
	ChooseChoiceIndex( InChoice->GetIndex() );
}

void UInkpotStory::ChooseChoiceIndex( int32 InChoiceIndex )
{
	FActiveStoryScope scope(this);
	StoryInternal->ChooseChoiceIndex( InChoiceIndex );
}

bool UInkpotStory::SwitchFlow( const FString &InFlowName )
{
	bool bIsNewFlow = !IsFlowAlive( InFlowName );
	StoryInternal->SwitchFlow( InFlowName );
	OnFlowChangeInternal();
	return bIsNewFlow;
}

bool UInkpotStory::SwitchFlowToPath(const FString& InFlowName, const FString& InPath, bool bInRestart)
{
	bool bIsNewFlow = SwitchFlow(InFlowName);
	if (bIsNewFlow || bInRestart)
	{
		ChoosePath(InPath);
	}
	return bIsNewFlow;
}

bool UInkpotStory::SwitchFlowToPathGT(const FString& InFlowName, FGameplayTag InPath, bool bInRestart)
{
	FString path = UInkpotGameplayTagLibrary::PathTagToString(InPath);
	return SwitchFlowToPath(InFlowName, path, bInRestart);
}

void UInkpotStory::RemoveFlow(const FString& InFlowName)
{
	FString currentFlow = GetCurrentFlowName();
	StoryInternal->RemoveFlow( InFlowName );
	if( !InFlowName.Equals( currentFlow ) )
		return;
	OnFlowChangeInternal();
}

FString UInkpotStory::GetCurrentFlowName()
{
	return StoryInternal->GetCurrentFlowName();
}

void UInkpotStory::SwitchToDefaultFlow()
{
	StoryInternal->SwitchToDefaultFlow();
	OnFlowChangeInternal();
}

void UInkpotStory::OnFlowChangeInternal()
{
	UpdateChoices();
	BroadcastFlowChange();
	DebugRefresh();
}

void UInkpotStory::BroadcastFlowChange()
{
	if(!EventOnSwitchFlow.IsBound())
		return;
	FString currentFlow = GetCurrentFlowName();
	EventOnSwitchFlow.Broadcast( this, currentFlow );
}

bool UInkpotStory::IsFlowAlive( const FString &InFlowName )
{
	TSharedPtr<Ink::FStoryState> state = StoryInternal->GetStoryState();
	TSharedPtr<TMap<FString, TSharedPtr<Ink::FFlow>>> namedFlows = state->GetNamedFlows();
	if( namedFlows.IsValid() )
		return namedFlows->Contains( InFlowName );
	return false;
}

const TArray<FString> &UInkpotStory::GetAliveFlowNames()
{
	TSharedPtr<Ink::FStoryState> state = StoryInternal->GetStoryState();
	return state->GetAliveFlowNames();
}

int32 UInkpotStory::GetAliveFlowCount()
{
	TSharedPtr<Ink::FStoryState> state = StoryInternal->GetStoryState();
	TSharedPtr<TMap<FString, TSharedPtr<Ink::FFlow>>> namedFlows = state->GetNamedFlows();

	int32 count = 0;
	if( namedFlows.IsValid() )
		count = namedFlows->Num();
	return count; 
}

FGameplayTag UInkpotStory::GetKnotFromPathGT(const FGameplayTag InPath, bool& OutHasParentKnot)
{
	TArray< FGameplayTag > ParentTags;
	InPath.ParseParentTags(ParentTags);
	int32 ParentCount = ParentTags.Num();

	if (ParentCount < 2)
	{
		INKPOT_ERROR("'%s' is not a knot or stitch", *InPath.ToString());
		OutHasParentKnot = false;
		return InPath;
	}
	else if (ParentCount == 2)
	{
		OutHasParentKnot = false;
		return InPath;
	}
	OutHasParentKnot = true;
	return InPath.RequestDirectParent();
}

void UInkpotStory::SetValue(const FString &InVariable, FInkpotValue InValue, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	OutSuccess = variableState->SetVariable( InVariable, Ink::FValue::Create(**InValue) );
}

void UInkpotStory::SetValueGT(FGameplayTag InVariable, FInkpotValue InValue )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	variableState->SetVariable( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), Ink::FValue::Create(**InValue) );
}

void UInkpotStory::GetValue(const FString &InVariable, FInkpotValue &OutReturnValue, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	TSharedPtr<Ink::FObject> variableObj = variableState->GetVariable( InVariable );
	TSharedPtr<Ink::FValue> variable = StaticCastSharedPtr<Ink::FValue>( variableObj );
	Ink::FValueType value;
	if(variable.IsValid())
	{
		value = variable->GetValueObject();
		OutSuccess = true;
	}
	else
	{
		INKPOT_ERROR("Could not get value of variable '%s'", *InVariable );
		OutSuccess = false;
	}
	OutReturnValue = FInkpotValue(value);
}

void UInkpotStory::GetValueGT(FGameplayTag InVariable, FInkpotValue &OutReturnValue )
{
	bool tmp;
	GetValue( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), OutReturnValue, tmp);
}

void UInkpotStory::SetBool(const FString &InVariable, bool bInValue, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	OutSuccess = variableState->SetVariable( InVariable, MakeShared<Ink::FValueBool>( bInValue ) );
}

void UInkpotStory::SetBoolGT(FGameplayTag InVariable, bool bInValue)
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	variableState->SetVariable( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), MakeShared<Ink::FValueBool>( bInValue ) );
}

void UInkpotStory::GetBool(const FString &InVariable, bool &OutReturnValue, bool &OutSuccess )
{
	OutSuccess = GetVariable<bool, Ink::FValueBool>(InVariable, Ink::EValueType::Bool, OutReturnValue );
}

void UInkpotStory::GetBoolGT(FGameplayTag InVariable, bool &OutReturnValue)
{
	GetVariable<bool, Ink::FValueBool>( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), Ink::EValueType::Bool, OutReturnValue );
}

void UInkpotStory::SetInt(const FString &InVariable, int32 InValue, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	OutSuccess = variableState->SetVariable( InVariable, MakeShared<Ink::FValueInt>( InValue ) );
}

void UInkpotStory::SetIntGT(FGameplayTag InVariable, int32 InValue )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	variableState->SetVariable( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), MakeShared<Ink::FValueInt>( InValue ) );
}

void UInkpotStory::GetInt(const FString &InVariable, int32 &OutValue ,  bool &OutSuccess )
{
	OutSuccess = GetVariable<int32, Ink::FValueInt>(InVariable, Ink::EValueType::Int, OutValue );
}

void UInkpotStory::GetIntGT(FGameplayTag InVariable, int32 &OutValue)
{
	GetVariable<int32, Ink::FValueInt>( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), Ink::EValueType::Int, OutValue );
}

void UInkpotStory::SetFloat(const FString &InVariable, float InValue, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	OutSuccess = variableState->SetVariable( InVariable, MakeShared<Ink::FValueFloat>(InValue));
}

void UInkpotStory::SetFloatGT(FGameplayTag InVariable, float InValue)
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	variableState->SetVariable( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), MakeShared<Ink::FValueFloat>(InValue));
}

void UInkpotStory::GetFloat(const FString &InVariable, float &OutReturnValue, bool &OutSuccess )
{
	OutSuccess = GetVariable<float, Ink::FValueFloat>(InVariable, Ink::EValueType::Float, OutReturnValue );
}

void UInkpotStory::GetFloatGT(FGameplayTag InVariable, float &OutReturnValue)
{
	GetVariable<float, Ink::FValueFloat>( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), Ink::EValueType::Float, OutReturnValue );
}

void UInkpotStory::SetString(const FString &InVariable, const FString &InValue, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	OutSuccess = variableState->SetVariable( InVariable, MakeShared<Ink::FValueString>(InValue) );
}

void UInkpotStory::SetStringGT(FGameplayTag InVariable, const FString &InValue)
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	variableState->SetVariable( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), MakeShared<Ink::FValueString>(InValue) );
}

void UInkpotStory::GetString( const FString& InVariable, FString &OutReturnValue, bool &OutSuccess )
{
	OutSuccess = GetVariable<FString, Ink::FValueString>(InVariable, Ink::EValueType::String, OutReturnValue );
}

void UInkpotStory::GetStringGT( FGameplayTag InVariable, FString &OutReturnValue)
{
	GetVariable<FString, Ink::FValueString>(UInkpotGameplayTagLibrary::VariableTagToString(InVariable), Ink::EValueType::String, OutReturnValue );
}

void UInkpotStory::SetEmpty(const FString &InVariable, bool &OutSuccess )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	OutSuccess  = variableState->SetVariable( InVariable, MakeShared<Ink::FValue>() );
}

void UInkpotStory::SetEmptyGT(FGameplayTag InVariable)
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	variableState->SetVariable( UInkpotGameplayTagLibrary::VariableTagToString(InVariable), MakeShared<Ink::FValue>() );
}

bool UInkpotStory::IsVariableDefined( const FString& InVariable )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	TSharedPtr<Ink::FObject> variable = variableState->GetVariable(  InVariable );
	return variable.IsValid();
}

bool UInkpotStory::IsVariableDefinedGT( FGameplayTag InVariable )
{
	return IsVariableDefined( UInkpotGameplayTagLibrary::VariableTagToString(InVariable) );
}

int32 UInkpotStory::GetVariableKeys( TArray<FString> &OutKeys )
{
	TSharedPtr<Ink::FVariableState> variableState = StoryInternal->GetVariablesState();
	const TMap<FString, TSharedPtr<Ink::FObject>>& variables = variableState->GetVariables();
	int32 rval = variables.GetKeys( OutKeys );
	return rval;
}

void UInkpotStory::ObserveVariable( const FString& InVariable, TSharedPtr<FStoryVariableObserver> InObserver )
{
	StoryInternal->ObserveVariable( InVariable, InObserver );
}

void UInkpotStory::SetOnVariableChange( FOnInkpotVariableChange InDelegate, const FString &InVariable )
{
	TSharedPtr<FStoryVariableObserver> observer = MakeShared<FStoryVariableObserver>();
	observer->BindWeakLambda
	(
		InDelegate.GetUObject(), [this, InDelegate] (const FString& InVar, Ink::FValueType InType)
		{
			InDelegate.Execute( this, InVar, FInkpotValue(InType) );
		}
	);
	
	VariableObservers.AddUnique( InVariable, observer );
	FActiveStoryScope scope(this);
	StoryInternal->ObserveVariable( InVariable, observer );
}

void UInkpotStory::SetOnVariableChangeGT( FOnInkpotVariableChange InDelegate, FGameplayTag InVariable )
{
	SetOnVariableChange( InDelegate, UInkpotGameplayTagLibrary::VariableTagToString(InVariable) );
}

bool UInkpotStory::ClearVariableChange( const UObject* Owner, const FString &Variable )
{
	if (!Owner || !VariableObservers.Contains(Variable))
		return false;

	bool bRemovedAtLeastOneEntry = false;
	
	for (auto Iterator = VariableObservers.CreateConstKeyIterator(Variable); Iterator; ++Iterator)
	{
		if (Iterator->Value.IsValid() && Owner == Iterator->Value->GetUObject())
		{
			StoryInternal->RemoveVariableObserver( Iterator->Value, Variable );
			VariableObservers.RemoveSingle( Variable, Iterator->Value );
			bRemovedAtLeastOneEntry = true;
		}
	}

	return bRemovedAtLeastOneEntry;
}

bool UInkpotStory::ClearVariableChangeGT( const UObject* Owner, FGameplayTag Variable )
{
	return ClearVariableChange(Owner,UInkpotGameplayTagLibrary::VariableTagToString(Variable) );
}

void UInkpotStory::ClearAllVariableObservers( const FString& Variable )
{
	if (!VariableObservers.Contains(Variable))
		return;
	
	for (auto Iterator = VariableObservers.CreateConstKeyIterator(Variable); Iterator; ++Iterator)
	{
		StoryInternal->RemoveVariableObserver( Iterator->Value, Variable );
	}

	VariableObservers.Remove(Variable);
}

void UInkpotStory::ClearAllVariableObserversGT( FGameplayTag Variable )
{
	ClearAllVariableObservers(UInkpotGameplayTagLibrary::VariableTagToString(Variable));
}

void UInkpotStory::OnContinueInternal()
{
	if (!bIsInFunctionEvaluation)
	{
		UpdateChoices();
		DumpDebug();

		if (EventOnContinue.IsBound())
			EventOnContinue.Broadcast(this);

		// Natural completion: the current flow has run out of content and has no
		// choices. Reported per flow so listeners can tear down without polling.
		// Uses the raw VM state rather than CanContinue()/HasChoices() because those
		// are gated by line rendering.
		// Deferred until the last line of the flow has finished rendering, so the
		// event arrives after the final line is presented, not while it is in flight.
		if (!StoryInternal->CanContinue() && !StoryInternal->HasChoices())
		{
			NotifyFlowEndedOrDefer(GetCurrentFlowName());
		}
	}
	DebugRefresh();
}

void UInkpotStory::OnMakeChoiceInternal(TSharedPtr<Ink::FChoice> InChoice)
{
	if(!EventOnContinue.IsBound())
		return;
	int32 key = InChoice->GetIndex();
	TObjectPtr<UInkpotChoice>* choicePtr = Choices.FindByPredicate([key](TObjectPtr<UInkpotChoice> pChoice) { return *pChoice == key; });
	if(!choicePtr)
		return;
	DumpDebug( *choicePtr );
	EventOnMakeChoice.Broadcast( this, *choicePtr );
}

void UInkpotStory::OnEvaluateFunctionInternal(const FString& InFunctionName, const TArray<TSharedPtr<Ink::FValueType>>& InFunctionParms)
{
	//INKPOT_LOG("Starting function %s", *InFunctionName);
	bIsInFunctionEvaluation = true;
}

void UInkpotStory::OnCompleteEvaluateFunctionInternal(const FString& InFunctionName, const TArray<TSharedPtr<Ink::FValueType>>& InFunctionParms, const FString& OutParmName, TSharedPtr<Ink::FValueType> OutParmType)
{
	//INKPOT_LOG("Completing function %s", *InFunctionName);
	bIsInFunctionEvaluation = false;
}

void UInkpotStory::OnChoosePathStringInternal(const FString& InPath, const TArray<TSharedPtr<Ink::FValueType>>& InPathType )
{
	UpdateChoices();
	if(EventOnChoosePath.IsBound())
		EventOnChoosePath.Broadcast( this, InPath ); 
	DebugRefresh();
}

void UInkpotStory::DumpDebug()
{
	FString currentFlow = GetCurrentFlowName();
	INKPOT_LOG("Flow         : %s", *currentFlow);

	if( GetAliveFlowCount() > 0 )
	{
		TArray<FString> flows = GetAliveFlowNames();
		if ( flows.Num() > 0 )
		{
			FString flowsAlive;
			for ( auto flow : flows )
			{
				flowsAlive.Append( flow );
				flowsAlive.Append( ", " );
			}
			INKPOT_LOG("Flows alive  : %s", *flowsAlive );
		}
	}

	FString currentText = GetCurrentText();
	INKPOT_LOG("Text         : %s", *currentText);

	const TArray<FString>& tags = GetCurrentTags();
	if( tags.Num() > 0 )
	{
		FString tagsSet;
		for( auto tag : tags )
		{
			tagsSet.Append( "'#" );
			tagsSet.Append( tag );
			tagsSet.Append( "' " );
		}
		INKPOT_LOG("CTags        : %s", *tagsSet);
	}

	TArray<FString> gtags = GlobalTags();
	if( gtags.Num() > 0 )
	{
		FString tagsSet;
		for( auto tag : gtags )
		{
			tagsSet.Append( "'#" );
			tagsSet.Append( tag );
			tagsSet.Append( "' " );
		}
		INKPOT_LOG("GTags        : %s", *tagsSet);
	}

	TArray<FString> keys;
	if(int num = GetVariableKeys( keys ))
	{
		for( int32 i=0; i<num; ++i )
		{
			const FString& key = keys[i];
			TSharedPtr<Ink::FObject> obj = GetVariable( key );
			const FString& value = obj->ToString();
			if(i==0)
			{
				INKPOT_LOG("Variables    : %s = %s", *key, *value);
			}
			else
			{
				INKPOT_LOG("             : %s = %s", *key, *value);
			}
		}
	}

	if(Choices.Num() > 0)
	{
		for( int32 i=0; i<Choices.Num(); ++i )
		{
			FString tagsSet;
			for( auto tag : Choices[i]->GetTags() )
			{
				tagsSet.Append( "'#" );
				tagsSet.Append( tag );
				tagsSet.Append( "' " );
			}
			if( i == 0 ) 
			{
				INKPOT_LOG( "Choice       : %d - %s  %s", Choices[ i ]->GetIndex(), *Choices[ i ]->GetString(), *tagsSet );
			}
			else
			{
				INKPOT_LOG( "             : %d - %s  %s", Choices[ i ]->GetIndex(), *Choices[ i ]->GetString(), *tagsSet );
			}
		}
	}
}

void UInkpotStory::DumpDebug(UInkpotChoice *InChoice)
{
	if(!InChoice)
		return;
	INKPOT_LOG("Chose        : %d - %s", InChoice->GetIndex(), *InChoice->GetString());
}


void UInkpotStory::DumpContainer(const FString &InName, TSharedPtr<Ink::FContainer> InContainer, int Indent)
{
	if(!InContainer)
		return;

	FString pad; 
	for(int i=0;i<Indent;++i)
		pad += '\t';

	INKPOT_LOG("%s** container(%s) - Begin", *pad,*InName );
	pad += '\t';

	TArray<TSharedPtr<Ink::FObject>> *contents = InContainer->GetContent().Get();
	TSet<TSharedPtr<Ink::FContainer>> containersDumped;

	for( TSharedPtr<Ink::FObject> obj : *contents)
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>(obj);
		if( container )
		{
			containersDumped.Add(container);
			FString name = container->GetPath()->ToString();
			if (name.IsEmpty())
				name = "inline";
			DumpContainer(name, container, Indent + 1 );
		}
		else
		{
			FString entry = obj->ToString();
			entry.TrimEndInline();
			if ( entry.Len() )
				INKPOT_LOG( "%s%s", *pad, *entry );
		}
	}

	TSharedPtr<TMap<FString, TSharedPtr<Ink::FObject>>> namedContentPtr = InContainer->GetNamedContent();
	for( auto pair : *namedContentPtr )
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>( pair.Value );
		if(!container)
			continue;
		if(containersDumped.Contains(container))
			continue;
		DumpContainer( pair.Key, container, Indent + 1 );
	}

	pad.LeftChopInline(1);
	INKPOT_LOG("%s** container(%s) - End", *pad, *InName );
}


void UInkpotStory::DumpMainContent()
{
	TSharedPtr<Ink::FContainer> main = StoryInternal->GetMainContentContainer();
	DumpContainer( "<root>", main );
}

void UInkpotStory::DumpContentAtPath( const FString& InName )
{
	TSharedPtr<Ink::FPath> path = MakeShared<Ink::FPath>( InName );
	Ink::FSearchResult result = StoryInternal->ContentAtPath(path);
	DumpContainer(InName, result.GetObjectAsContainer() );
}

void UInkpotStory::DumpContentAtKnot( const FString& InName )
{
	TSharedPtr<Ink::FContainer> knotContainer = StoryInternal->KnotContainerWithName(InName);
	if(!knotContainer)
	{
		INKPOT_ERROR("%s is not a valid knot in this story", *InName );
		return;
	}
	DumpContainer(InName, knotContainer  );
}

void UInkpotStory::DumpMainContentPaths()
{
	TSharedPtr<Ink::FContainer> main = StoryInternal->GetMainContentContainer();
	DumpContainerPaths("<root>", main);
}

void UInkpotStory::DumpContainerPaths(const FString& InName, TSharedPtr<Ink::FContainer> InContainer, int Indent)
{
	if (!InContainer)
		return;

	FString pad;
	for (int i = 0; i < Indent; ++i)
		pad += '\t';

	INKPOT_LOG("%s%s", *pad, *InName);
	TArray<TSharedPtr<Ink::FObject>>* contents = InContainer->GetContent().Get();
	TSet<TSharedPtr<Ink::FContainer>> containersDumped;

	for (TSharedPtr<Ink::FObject> obj : *contents)
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>(obj);
		if (container)
		{
			containersDumped.Add(container);
			FString nameAlso = InContainer->GetPath()->ToString();
			static volatile bool breakMe = false;
			if (container->GetName().Equals(TEXT("Intro")))
				breakMe = true;

			FString name = container->GetPath()->ToString();
			if (name.IsEmpty())
				name = "inline";
			DumpContainerPaths(name, container, Indent + 1);
		}
	}

	TSharedPtr<TMap<FString, TSharedPtr<Ink::FObject>>> namedContentPtr = InContainer->GetNamedContent();
	for (auto pair : *namedContentPtr)
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>(pair.Value);
		if (!container)
			continue;
		if (containersDumped.Contains(container))
			continue;
		DumpContainerPaths(pair.Key, container, Indent + 1);
	}
}

void UInkpotStory::GatherAllStrings( TMap<FString, FString> &OutStrings )
{
	TSharedPtr<Ink::FContainer> main = StoryInternal->GetMainContentContainer();
	GatherAllStrings( "", main, OutStrings);
}

void UInkpotStory::GatherAllStrings( const FString &InRootName, TSharedPtr<Ink::FContainer> InContainer, TMap<FString, FString> &OutStrings )
{
	if(!InContainer)
		return;

	FString rootName = InRootName;
	FString containerName = InContainer->GetName();
	if (rootName.IsEmpty())
		rootName = containerName;
	else if (containerName.Len() > 0)
		rootName += TEXT(".") + containerName;

	TArray<TSharedPtr<Ink::FObject>> *contents = InContainer->GetContent().Get();
	int contentIndex = 1;
	for( TSharedPtr<Ink::FObject> obj : *contents)
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>(obj);
		if( container )
		{
			GatherAllStrings( rootName, container, OutStrings  );
		}
		else
		{
			if(obj->CanCastTo(Ink::EInkObjectClass::FValueString))
			{
				FString entry = obj->ToString();
				entry.TrimEndInline();
				if ( entry.Len() )
				{
					FString key = FString::Printf(TEXT("%s.%02d"), *rootName, contentIndex);
					OutStrings.Add( key, entry );
					INKPOT_LOG( "%s : %s", *key, *entry );
					contentIndex++;
				}
			}
		}
	}

	TSharedPtr<TMap<FString, TSharedPtr<Ink::FObject>>> namedContentPtr = InContainer->GetNamedContent();
	for( auto pair : *namedContentPtr )
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>( pair.Value );
		if(!container)
			continue;
		GatherAllStrings( rootName, container, OutStrings );
	}
}

TArray<FString> UInkpotStory::GetNamedContent()
{
	TSharedPtr<Ink::FContainer> container = StoryInternal->GetMainContentContainer();
	return GetNamedContent(container);
}

TArray<FString> UInkpotStory::GetNamedContentForKnot(const FString& InKnotName)
{
	TSharedPtr<Ink::FContainer> knotContainer = StoryInternal->KnotContainerWithName(InKnotName);
	return GetNamedContent(knotContainer);
}

TArray<FString> UInkpotStory::GetNamedContentForPath(const FString& InPath)
{
	TSharedPtr<Ink::FPath> path = MakeShared<Ink::FPath>(InPath);
	Ink::FSearchResult result = StoryInternal->ContentAtPath(path);
	return GetNamedContent(result.GetObjectAsContainer());
}

TArray<FString> UInkpotStory::GetNamedContent(TSharedPtr<Ink::FContainer> InContainer)
{
	TArray<FString> keys;
	if (!InContainer)
	{
		INKPOT_ERROR("Container is invalid, no content.");
		return keys;
	}

	TSharedPtr<TMap<FString, TSharedPtr<Ink::FObject>>> namedContentPtr = InContainer->GetNamedContent();
	for (auto pair : *namedContentPtr)
	{
		TSharedPtr<Ink::FContainer> container = Ink::FObject::DynamicCastTo<Ink::FContainer>(pair.Value);
		if (!container)
			continue;
		keys.Add(pair.Key);
	}

	return keys;
}

void UInkpotStory::BindExternalFunction( const FString &InFunctionName, FInkpotExternalFunction InFunction, bool bInLookAheadSafe  )
{
	if (StoryInternal->IsExternalFunctionBound(InFunctionName))
	{
		INKPOT_WARN("Function '%s' is already bound, use UnbindExternalFunction to clear.", *InFunctionName);
		return;
	}

	TSharedPtr<FStoryExternalFunction> function = MakeShared<FStoryExternalFunction>();
	function->BindWeakLambda
	(
		InFunction.GetUObject(), [this, InFunction] (const TArray<Ink::FValueType> &InArguments) -> TSharedPtr<Ink::FValueType>
		{
			TArray<FInkpotValue> arguments;
			for(auto &argument : InArguments )
				arguments.Add( argument );

			FInkpotValue value = InFunction.Execute( arguments );
			return *value;
		}
	);
	StoryInternal->BindExternalFunctionGeneral( InFunctionName, function, bInLookAheadSafe );
}

void UInkpotStory::UnbindExternalFunction( const FString &InFunctionName )
{
	if (!StoryInternal->IsExternalFunctionBound(InFunctionName))
	{
		INKPOT_WARN("Function '%s' does not exist.", *InFunctionName);
		return;
	}

	StoryInternal->UnbindExternalFunction( InFunctionName );
}

void UInkpotStory::EvaluateFunction(const FString& FunctionName, const TArray<FInkpotValue> &InValues, FInkpotValue& OutReturnValue, FString& CapturedText)
{
	TArray<TSharedPtr<Ink::FValueType>> values;
	if (!CreateInkValues(InValues, values))
		return;
	FActiveStoryScope scope(this);
	TSharedPtr<Ink::FValueType> returnValue = StoryInternal->EvaluateFunction(FunctionName, CapturedText, values);
	OutReturnValue = returnValue;
}

void UInkpotStory::EvalFunction(const FString& FunctionName, FInkpotValue& OutReturnValue, FString& CapturedText)
{
	TArray<TSharedPtr<Ink::FValueType>> values;
	TSharedPtr<Ink::FValueType> returnValue = StoryInternal->EvaluateFunction(FunctionName, CapturedText, values);
	OutReturnValue = returnValue;
}
FOnStoryContinue& UInkpotStory::OnContinue()
{
	return EventOnContinue;
}

FOnMakeChoice& UInkpotStory::OnMakeChoice()
{
	return EventOnMakeChoice;
}

FOnChoosePath& UInkpotStory::OnChoosePath()
{
	return EventOnChoosePath;
}

FOnSwitchFlow& UInkpotStory::OnSwitchFlow()
{
	return EventOnSwitchFlow;
}

FOnStoryLoadJSON& UInkpotStory::OnStoryLoadJSON()
{
	return EventOnStoryLoadJSON;
}

FOnFlowEnded& UInkpotStory::OnFlowEnded()
{
	return EventOnFlowEnded;
}

void UInkpotStory::NotifyFlowEndedOrDefer(const FString& InFlowName)
{
	// If a line is still rendering for this flow, wait for NotifyLineRenderEnd to fire
	// the event so it arrives after the final line is presented.
	if (IsLineRenderingForFlow(InFlowName))
		FlowsPendingEnd.Add(InFlowName);
	else
		BroadcastFlowEnded(InFlowName);
}

void UInkpotStory::BroadcastFlowEnded(const FString& InFlowName)
{
	if (EventOnFlowEnded.IsBound())
		EventOnFlowEnded.Broadcast(this, InFlowName);
}

UInkpotLine *UInkpotStory::GetCurrentLine()
{
	UInkpotLine* line = NewObject<UInkpotLine>( this );
	line->Initialise( GetCurrentText() );
	return line;
}

void UInkpotStory::SetProsettaMetadata( UProsettaMetadata *InMetadata )
{
	ProsettaMetadata = InMetadata;
}

UProsettaMetadata *UInkpotStory::GetProsettaMetadata() const
{
	return ProsettaMetadata;
}

void UInkpotStory::ResetContent( TSharedPtr<FInkpotStoryInternal> InNewStoryContent )
{
	StoryInternal->ResetContent( InNewStoryContent );
	// New content may call different EXTERNALs; external bindings themselves survive the reset.
	bExternalBindingsValidated = false;
}

void UInkpotStory::ResetState()
{
	StoryInternal->ResetState();
	BindOnVariableStateChangeEvent();
}

FString UInkpotStory::ToJSON()
{
	return StoryInternal->GetStoryState()->ToJson();
}

void UInkpotStory::LoadJSON(const FString &InJSON)
{
	StoryInternal->GetStoryState()->LoadJSON( InJSON );
	UpdateChoices();
	if(EventOnStoryLoadJSON.IsBound())
		EventOnStoryLoadJSON.Broadcast( this );
}

void UInkpotStory::SetList( const FString& InVariable, const FInkpotList &InValue, bool &bOutSuccess )
{
	bOutSuccess = InValue.ValidateOrigin( this );
	if( bOutSuccess )
		SetValue( InVariable, *InValue, bOutSuccess );
}

void UInkpotStory::SetListGT( const FGameplayTag& InVariable, const FInkpotList &InValue)
{
	bool bOutSuccess = InValue.ValidateOrigin( this );
	if( bOutSuccess )
		SetValueGT( InVariable, *InValue );
}

void UInkpotStory::GetList( const FString& InVariable, FInkpotList &ReturnValue, bool &bOutSuccess )
{
	FInkpotValue value;
	GetValue( InVariable,  value, bOutSuccess);
	if( (*value)->HasSubtype<Ink::FInkList>() )
	{
		ReturnValue = *value; // copy ref 
	}
	else
	{
		INKPOT_ERROR("Variable '%s' is not a list type.", *InVariable );
		bOutSuccess = false;
	}
}

void UInkpotStory::GetListGT( FGameplayTag InVariable, FInkpotList &ReturnValue )
{
	FInkpotValue value;
	GetValueGT( InVariable,  value);
	if( (*value)->HasSubtype<Ink::FInkList>() )
		ReturnValue = *value; // copy ref 
}

int UInkpotStory::GetStorySeed() const
{
	return StoryInternal->GetStoryState()->GetStorySeed();
}

void UInkpotStory::SetStorySeed( int Seed )
{
	StoryInternal->GetStoryState()->SetStorySeed( Seed );
}

void UInkpotStory::BindOnVariableStateChangeEvent()
{
	StoryInternal->GetVariablesState()->VariableChanged.AddUObject(this, &UInkpotStory::OnVariableStateChangeEvent);
}

void UInkpotStory::BindOnStoryErrorEvent()
{
	if (!StoryInternal->OnError().IsBoundToObject(this))
		StoryInternal->OnError().AddUObject(this, &UInkpotStory::OnStoryErrorInternal);
}

void UInkpotStory::OnStoryErrorInternal(FString InMessage, Ink::EErrorType InErrorType)
{
	EInkpotStoryErrorType type = EInkpotStoryErrorType::Error;
	switch (InErrorType)
	{
	case Ink::EErrorType::Author:  type = EInkpotStoryErrorType::Author; break;
	case Ink::EErrorType::Warning: type = EInkpotStoryErrorType::Warning; break;
	default: break;
	}
	ReportStoryError( InMessage, type );
}

namespace
{
	// The story whose Ink VM is executing on this thread, for attributing VM exceptions.
	thread_local UInkpotStory* GActiveInkpotStory = nullptr;
}

UInkpotStory::FActiveStoryScope::FActiveStoryScope( UInkpotStory* InStory )
: Previous( GActiveInkpotStory )
{
	GActiveInkpotStory = InStory;
}

UInkpotStory::FActiveStoryScope::~FActiveStoryScope()
{
	GActiveInkpotStory = Previous;
}

void UInkpotStory::OnRuntimeExceptionStatic( const FString &InMessage )
{
	if (UInkpotStory* story = GActiveInkpotStory)
	{
		story->ReportStoryError( TEXT("Ink runtime exception: ") + InMessage, EInkpotStoryErrorType::Error );
		return;
	}
	// No story is executing ( construction, tests driving Ink::FStory directly ): keep the old behaviour.
	INKPOT_ERROR("Ink runtime exception: %s", *InMessage);
}

void UInkpotStory::SetProsettaProvenance( const FProsettaStoryProvenance &InProvenance )
{
	ProsettaProvenance = InProvenance;
}

const FProsettaStoryProvenance& UInkpotStory::GetProsettaProvenance() const
{
	return ProsettaProvenance;
}

FString UInkpotStory::GetLogContext() const
{
	FString context = FString::Printf( TEXT("story %d"), GetID() );
	if (ProsettaProvenance.IsSet() && GetDefault<UInkpotSettings>()->bIncludeProsettaProvenanceInLogs)
		context += TEXT(" | ") + ProsettaProvenance.ToLogString();
	return context;
}

void UInkpotStory::ReportStoryError( const FString &InMessage, EInkpotStoryErrorType InErrorType )
{
	EventOnStoryErrorNative.Broadcast( this, InMessage, InErrorType );
	if (EventOnStoryError.IsBound())
		EventOnStoryError.Broadcast( this, InMessage, InErrorType );
	ApplyStoryErrorPolicy( InMessage, InErrorType );
}

void UInkpotStory::ApplyStoryErrorPolicy( const FString &InMessage, EInkpotStoryErrorType InErrorType )
{
	const UInkpotSettings* settings = GetDefault<UInkpotSettings>();
	const bool bError = InErrorType == EInkpotStoryErrorType::Error;
	const EInkpotStoryErrorAction action = bError ? settings->StoryErrorAction : settings->StoryWarningAction;
	if (action == EInkpotStoryErrorAction::BroadcastOnly)
		return;

	const FString message = FString::Printf( TEXT("[Inkpot %s] %s"), *GetLogContext(), *InMessage );
	if (bError)
	{
		INKPOT_ERROR("%s", *message);
	}
	else
	{
		INKPOT_WARN("%s", *message);
	}

	if (settings->bStoryErrorsToMessageLog)
	{
		FMessageLog log( "Inkpot" );
		TSharedRef<FTokenizedMessage> tokenized = bError ? log.Error() : log.Warning();
		tokenized->AddToken( FTextToken::Create( FText::FromString( message ) ) );
	}

	if (settings->bStoryErrorsOnScreen && GEngine)
		GEngine->AddOnScreenDebugMessage( INDEX_NONE, settings->StoryErrorOnScreenSeconds, bError ? FColor::Red : FColor::Yellow, message );

	if (action == EInkpotStoryErrorAction::Ensure)
	{
		ensureAlwaysMsgf( false, TEXT("%s"), *message );
	}
	else if (action == EInkpotStoryErrorAction::Fatal)
	{
		UE_LOG( Inkpot, Fatal, TEXT("%s"), *message );
	}
}

FOnInkpotStoryErrorNative& UInkpotStory::OnStoryError()
{
	return EventOnStoryErrorNative;
}

/* ---------------------------------------------------------------------------
 * External function discovery, validation and native binding
 * ------------------------------------------------------------------------- */

namespace
{
	void CollectExternalCalls(TSharedPtr<Ink::FContainer> InContainer, TMap<FString, int32>& OutExternals, TSet<Ink::FContainer*>& Visited)
	{
		if (!InContainer.IsValid() || Visited.Contains(InContainer.Get()))
			return;
		Visited.Add(InContainer.Get());

		auto visit = [&OutExternals, &Visited](TSharedPtr<Ink::FObject> InObject)
		{
			if (!InObject.IsValid())
				return;
			if (TSharedPtr<Ink::FDivert> divert = Ink::FObject::DynamicCastTo<Ink::FDivert>(InObject))
			{
				if (divert->IsExternal())
				{
					const FString name = divert->GetTargetPathString();
					if (!name.IsEmpty())
						OutExternals.FindOrAdd(name) = divert->GetExternalsArgs();
				}
				return;
			}
			if (TSharedPtr<Ink::FContainer> child = Ink::FObject::DynamicCastTo<Ink::FContainer>(InObject))
				CollectExternalCalls(child, OutExternals, Visited);
		};

		if (TSharedPtr<TArray<TSharedPtr<Ink::FObject>>> content = InContainer->GetContent())
		{
			for (const TSharedPtr<Ink::FObject>& object : *content)
				visit(object);
		}
		for (const auto& pair : InContainer->GetNamedOnlyContent())
			visit(pair.Value);
	}
}

bool UInkpotStory::IsExternalFunctionBound( const FString &InFunctionName ) const
{
	return StoryInternal.IsValid() && StoryInternal->IsExternalFunctionBound( InFunctionName );
}

TArray<FInkpotExternalDeclaration> UInkpotStory::GetDeclaredExternalFunctions() const
{
	TArray<FInkpotExternalDeclaration> declarations;
	if (!StoryInternal.IsValid())
		return declarations;

	TMap<FString, int32> externals;
	TSet<Ink::FContainer*> visited;
	CollectExternalCalls( StoryInternal->GetMainContent(), externals, visited );

	TArray<FString> names;
	externals.GetKeys( names );
	names.Sort();
	for (const FString& name : names)
	{
		FInkpotExternalDeclaration declaration;
		declaration.Name = name;
		declaration.Arity = externals[name];
		declaration.bHasFallback = StoryInternal->HasFunction( name );
		declarations.Add( MoveTemp(declaration) );
	}
	return declarations;
}

bool UInkpotStory::ValidateExternalBindings( TArray<FString> &OutUnbound, bool bLogErrors )
{
	OutUnbound.Reset();
	if (!StoryInternal.IsValid())
		return false;

	const bool bAllowFallbacks = StoryInternal->GetAllowExternalFunctionFallbacks();
	for (const FInkpotExternalDeclaration& external : GetDeclaredExternalFunctions())
	{
		if (StoryInternal->IsExternalFunctionBound( external.Name ))
			continue;
		if (bAllowFallbacks && external.bHasFallback)
			continue;
		OutUnbound.Add( external.Name );
		if (bLogErrors)
		{
			ReportStoryError( FString::Printf( TEXT("Calls EXTERNAL '%s' ( arity %d ) but nothing is bound to it%s. Bind it before Continue reaches it, or register a native binding module."),
				*external.Name, external.Arity, external.bHasFallback ? TEXT(" and Ink fallbacks are disabled") : TEXT("") ), EInkpotStoryErrorType::Error );
		}
	}
	bExternalBindingsValidated = true;
	return OutUnbound.Num() == 0;
}

void UInkpotStory::EnsureExternalBindingsValidated()
{
	if (bExternalBindingsValidated || !StoryInternal.IsValid())
		return;
	bExternalBindingsValidated = true;
	if (!GetDefault<UInkpotSettings>()->bValidateExternalBindingsOnFirstContinue)
		return;
	TArray<FString> unbound;
	ValidateExternalBindings( unbound, true );
}

bool UInkpotStory::BindExternalFunctionInternal( const FString &InFunctionName, TSharedPtr<FStoryExternalFunction> InFunction, bool bInLookAheadSafe, bool bInOverrideExisting )
{
	if (StoryInternal->IsExternalFunctionBound( InFunctionName ))
	{
		if (!bInOverrideExisting)
			return false;
		StoryInternal->UnbindExternalFunction( InFunctionName );
	}
	StoryInternal->BindExternalFunctionGeneral( InFunctionName, InFunction, bInLookAheadSafe );
	bExternalBindingsValidated = false;
	return true;
}

bool UInkpotStory::BindNativeFunction( const FInkpotNativeFunction &InFunction, bool bInOverrideExisting )
{
	if (!StoryInternal.IsValid() || !InFunction.Thunk)
		return false;

	TSharedPtr<FStoryExternalFunction> function = MakeShared<FStoryExternalFunction>();
	InkpotNativeBinding::FThunk thunk = InFunction.Thunk;
	function->BindLambda( [thunk](const TArray<Ink::FValueType> &InArguments) -> TSharedPtr<Ink::FValueType>
	{
		return thunk( InArguments );
	});

	if (!BindExternalFunctionInternal( InFunction.Descriptor.Name, function, InFunction.Descriptor.bLookaheadSafe, bInOverrideExisting ))
	{
		INKPOT_WARN("[Inkpot %s] Native function '%s' ( module %s ) not bound: '%s' is already bound.", *GetLogContext(), *InFunction.Descriptor.Name, *InFunction.Descriptor.ModuleName, *InFunction.Descriptor.Name);
		return false;
	}
	return true;
}

FInkpotNativeBindingReport UInkpotStory::BindRegisteredNativeFunctions( const TArray<FString> &InModuleFilter, bool bInOverrideExisting )
{
	FInkpotNativeBindingReport report;
	if (!StoryInternal.IsValid())
	{
		report.Messages.Add( TEXT("Story is not initialised.") );
		return report;
	}

	const FInkpotNativeBindingRegistry& registry = FInkpotNativeBindingRegistry::Get();
	const TArray<FInkpotExternalDeclaration> externals = GetDeclaredExternalFunctions();
	const bool bAllowFallbacks = StoryInternal->GetAllowExternalFunctionFallbacks();

	for (const FInkpotExternalDeclaration& external : externals)
	{
		const FInkpotNativeBindingModule* module = nullptr;
		const FInkpotNativeFunction* function = nullptr;

		// First registered module providing the name wins; a filter restricts the candidates.
		for (const FInkpotNativeBindingModule* candidate : registry.GetModules())
		{
			if (InModuleFilter.Num() > 0 && !InModuleFilter.Contains( candidate->Name ))
				continue;
			const FInkpotNativeFunction* found = candidate->Functions.FindByPredicate( [&external](const FInkpotNativeFunction& F) { return F.Descriptor.Name == external.Name; } );
			if (found)
			{
				module = candidate;
				function = found;
				break;
			}
		}

		if (!function)
		{
			if (!StoryInternal->IsExternalFunctionBound( external.Name ) && !(bAllowFallbacks && external.bHasFallback))
				report.Unbound.Add( external.Name );
			continue;
		}

		if (function->Descriptor.Arity != external.Arity)
		{
			report.ArityMismatch.Add( external.Name );
			report.Messages.Add( FString::Printf( TEXT("'%s' in module %s takes %d parameter(s) but the story calls it with %d; not bound."), *external.Name, *module->Name, function->Descriptor.Arity, external.Arity ) );
			ReportStoryError( FString::Printf( TEXT("Native function '%s' ( module %s ) takes %d parameter(s) but the story calls it with %d; not bound."), *external.Name, *module->Name, function->Descriptor.Arity, external.Arity ), EInkpotStoryErrorType::Error );
			if (!StoryInternal->IsExternalFunctionBound( external.Name ))
				report.Unbound.Add( external.Name );
			continue;
		}

		if (StoryInternal->IsExternalFunctionBound( external.Name ) && !bInOverrideExisting)
		{
			report.AlreadyBound.Add( external.Name );
			continue;
		}

		if (BindNativeFunction( *function, bInOverrideExisting ))
			report.Bound.Add( external.Name );
	}

	report.bComplete = report.Unbound.Num() == 0 && report.ArityMismatch.Num() == 0;
	INKPOT_LOG("[Inkpot %s] native bindings: %d bound, %d already bound, %d unbound, %d arity mismatch.", *GetLogContext(), report.Bound.Num(), report.AlreadyBound.Num(), report.Unbound.Num(), report.ArityMismatch.Num());
	return report;
}

void UInkpotStory::SetAllowExternalFunctionFallbacks( bool bInAllow )
{
	if (!StoryInternal.IsValid())
		return;
	StoryInternal->SetAllowExternalFunctionFallbacks( bInAllow );
	bExternalBindingsValidated = false;
}

bool UInkpotStory::GetAllowExternalFunctionFallbacks() const
{
	return StoryInternal.IsValid() && StoryInternal->GetAllowExternalFunctionFallbacks();
}

/* ---------------------------------------------------------------------------
 * Variable observation
 * ------------------------------------------------------------------------- */

TArray<FString> UInkpotStory::GetGlobalVariableNames() const
{
	TArray<FString> names;
	if (!StoryInternal.IsValid())
		return names;
	StoryInternal->GetVariablesState()->GetVariables().GenerateKeyArray( names );
	names.Sort();
	return names;
}

void UInkpotStory::SetOnAnyVariableChange( FOnInkpotVariableChange InDelegate )
{
	if (!StoryInternal.IsValid() || !InDelegate.IsBound())
		return;

	TSharedPtr<FStoryVariableObserver> observer = MakeShared<FStoryVariableObserver>();
	observer->BindWeakLambda
	(
		InDelegate.GetUObject(), [this, InDelegate] (const FString& InVar, Ink::FValueType InType)
		{
			InDelegate.Execute( this, InVar, FInkpotValue(InType) );
		}
	);
	AnyVariableObservers.Emplace( TWeakObjectPtr<const UObject>( InDelegate.GetUObject() ), observer );
	FActiveStoryScope scope(this);
	StoryInternal->ObserveAllVariables( observer );
}

bool UInkpotStory::ClearAnyVariableChange( const UObject* InOwner )
{
	if (!InOwner || !StoryInternal.IsValid())
		return false;
	bool bRemoved = false;
	for (int32 index = AnyVariableObservers.Num() - 1; index >= 0; --index)
	{
		if (AnyVariableObservers[index].Key.Get() == InOwner)
		{
			StoryInternal->RemoveVariableObserver( AnyVariableObservers[index].Value );
			AnyVariableObservers.RemoveAt( index );
			bRemoved = true;
		}
	}
	return bRemoved;
}

FInkpotVariableObserverHandle UInkpotStory::ObserveVariableNative( const FString &InVariable, FInkpotVariableChangeNative InCallback )
{
	FInkpotVariableObserverHandle handle;
	if (!StoryInternal.IsValid() || !InCallback)
		return handle;

	TSharedPtr<FStoryVariableObserver> observer = MakeShared<FStoryVariableObserver>();
	observer->BindWeakLambda
	(
		this, [this, InCallback] (const FString& InVar, Ink::FValueType InType)
		{
			InCallback( this, InVar, FInkpotValue(InType) );
		}
	);
	handle.Id = NextNativeObserverId++;
	NativeVariableObservers.Add( handle.Id, observer );
	FActiveStoryScope scope(this);
	StoryInternal->ObserveVariable( InVariable, observer );
	return handle;
}

FInkpotVariableObserverHandle UInkpotStory::ObserveAllVariablesNative( FInkpotVariableChangeNative InCallback )
{
	FInkpotVariableObserverHandle handle;
	if (!StoryInternal.IsValid() || !InCallback)
		return handle;

	TSharedPtr<FStoryVariableObserver> observer = MakeShared<FStoryVariableObserver>();
	observer->BindWeakLambda
	(
		this, [this, InCallback] (const FString& InVar, Ink::FValueType InType)
		{
			InCallback( this, InVar, FInkpotValue(InType) );
		}
	);
	handle.Id = NextNativeObserverId++;
	NativeVariableObservers.Add( handle.Id, observer );
	FActiveStoryScope scope(this);
	StoryInternal->ObserveAllVariables( observer );
	return handle;
}

bool UInkpotStory::RemoveVariableObserver( const FInkpotVariableObserverHandle &InHandle )
{
	TSharedPtr<FStoryVariableObserver> observer;
	if (!InHandle.IsValid() || !NativeVariableObservers.RemoveAndCopyValue( InHandle.Id, observer ))
		return false;
	if (StoryInternal.IsValid())
		StoryInternal->RemoveVariableObserver( observer );
	return true;
}

void UInkpotStory::OnVariableStateChangeEvent(const FString& VariableName, TSharedPtr<Ink::FObject> NewValueObj)
{
	DebugRefresh();
}

void UInkpotStory::DebugRefresh()
{
#if WITH_EDITOR
	if (!EventOnDebugRefresh.IsBound())
		return;
	EventOnDebugRefresh.Broadcast(this);
#endif
}

#if WITH_EDITOR
FOnStoryContinue& UInkpotStory::OnDebugRefresh()
{
	return EventOnDebugRefresh;
}
#endif

void UInkpotStory::NotifyLineRenderBegin(FName InContext)
{
	// Record the flow that owns this render so the gate can be scoped per flow.
	LineRenderContextsInFlight.Add( InContext, GetCurrentFlowName() );
}

void UInkpotStory::NotifyLineRenderEnd(FName InContext, bool bInSuccess)
{
	const FString* OwnerFlow = LineRenderContextsInFlight.Find(InContext);
	if (!OwnerFlow)
	{
		INKPOT_ERROR("Could not find context for end of line render. '%s'", *InContext.ToString() );
		return;
	}

	FString Flow = *OwnerFlow;
	LineRenderContextsInFlight.Remove(InContext);

	if (EventOnLineComplete.IsBound())
		EventOnLineComplete.Broadcast(this, InContext, bInSuccess);

	// If this flow was waiting to report its end until rendering finished, and this was
	// the last render in flight for it, fire OnFlowEnded now.
	if (FlowsPendingEnd.Contains(Flow) && !IsLineRenderingForFlow(Flow))
	{
		FlowsPendingEnd.Remove(Flow);
		BroadcastFlowEnded(Flow);
	}
}

bool UInkpotStory::IsLineRendering() const
{
	return LineRenderContextsInFlight.Num() > 0;
}

bool UInkpotStory::IsLineRenderingForFlow(const FString& InFlowName) const
{
	for (const TPair<FName, FString>& Context : LineRenderContextsInFlight)
	{
		if (Context.Value == InFlowName)
			return true;
	}
	return false;
}
