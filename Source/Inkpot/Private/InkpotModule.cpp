#include "InkpotModule.h"
#include "Inkpot/InkpotStory.h"
#include "Ink/Object.h"

#define LOCTEXT_NAMESPACE "InkpotModule"

IMPLEMENT_MODULE(FInkpotModule, Inkpot)

void FInkpotModule::StartupModule()
{
	// Route InkPlusPlus exception-substitute messages through the story error policy.
	ExceptionHandle = Ink::FObject::StoryException.OnThrow.AddStatic(&UInkpotStory::OnRuntimeExceptionStatic);
}

void FInkpotModule::ShutdownModule()
{
	Ink::FObject::StoryException.OnThrow.Remove(ExceptionHandle);
}

#undef LOCTEXT_NAMESPACE 
