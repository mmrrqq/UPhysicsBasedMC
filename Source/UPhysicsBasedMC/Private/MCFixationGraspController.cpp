// Copyright 2018, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#include "MCFixationGraspController.h"
#include "Kismet/GameplayStatics.h"
#if ENGINE_MINOR_VERSION >= 19
#include "XRMotionControllerBase.h" // 4.19
#endif

// Constructor, set default values
UMCFixationGraspController::UMCFixationGraspController()
{
	InitSphereRadius(3.f);
	bGenerateOverlapEvents = true;
	bWeldFixation = true;
	ObjectMaxLength = 50.f;
	ObjectMaxMass = 15.f;	
}

// Called when the game starts or when spawned
void UMCFixationGraspController::BeginPlay()
{
	Super::BeginPlay();
}

// Init fixation grasp	
void UMCFixationGraspController::Init(USkeletalMeshComponent* InHand, UMotionControllerComponent* InMC, UInputComponent* InIC)
{
	// Set pointer of skeletal hand
	SkeletalHand = InHand;

	// Setup input
	if (InIC)
	{
		SetupInputBindings(InMC, InIC);
	}
	else
	{
		// Get the input controller for mapping the grasping control inputs
		APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		if (PC)
		{
			UInputComponent* IC = PC->InputComponent;
			if (IC)
			{
				SetupInputBindings(InMC, IC);
			}
		}
	}
	
	// Bind overlap events
	OnComponentBeginOverlap.AddDynamic(this, &UMCFixationGraspController::OnFixationGraspAreaBeginOverlap);
	OnComponentEndOverlap.AddDynamic(this, &UMCFixationGraspController::OnFixationGraspAreaEndOverlap);
}

// Setup input bindings
void UMCFixationGraspController::SetupInputBindings(UMotionControllerComponent* InMC, UInputComponent* InIC)
{
	// Check hand type
#if ENGINE_MINOR_VERSION >= 19
	if (InMC->MotionSource == FXRMotionControllerBase::LeftHandSourceId)
#else
	if(InMC->Hand == EControllerHand::Left)
#endif
	{
		InIC->BindAction("LeftFixate", IE_Pressed, this, &UMCFixationGraspController::TryToFixate);
		InIC->BindAction("LeftFixate", IE_Released, this, &UMCFixationGraspController::TryToDetach);
	}
#if ENGINE_MINOR_VERSION >= 19
	if (InMC->MotionSource == FXRMotionControllerBase::RightHandSourceId)
#else
	if (InMC->Hand == EControllerHand::Right)
#endif
	{
		InIC->BindAction("RightFixate", IE_Pressed, this, &UMCFixationGraspController::TryToFixate);
		InIC->BindAction("RightFixate", IE_Released, this, &UMCFixationGraspController::TryToDetach);
	}

}

// Try to fixate object to hand
void UMCFixationGraspController::TryToFixate()
{
	while (!FixatedObject && ObjectsInReach.Num() > 0)
	{
		// Pop a SMA
		AStaticMeshActor* SMA = ObjectsInReach.Pop();

		// Check if the actor is graspable
		if (CanBeGrasped(SMA))
		{
			FixateObject(SMA);
		}		
	}
}

// Fixate object to hand
void UMCFixationGraspController::FixateObject(AStaticMeshActor* InSMA)
{
	// Disable physics and overlap events
	UStaticMeshComponent* SMC = InSMA->GetStaticMeshComponent();
	SMC->SetSimulatePhysics(false);
	//SMC->bGenerateOverlapEvents = false;
	
	InSMA->AttachToComponent(SkeletalHand, FAttachmentTransformRules(
	EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, bWeldFixation));
	//SMC->AttachToComponent(SkeletalHand, FAttachmentTransformRules(
	//	EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, bWeldFixation));
	//InSMA->AttachToActor(SkeletalHand, FAttachmentTransformRules(
	//	EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, bWeldFixation));

	// Disable overlap checks during fixation grasp
	bGenerateOverlapEvents = false;

	// Set the fixated object
	FixatedObject = InSMA;

	// Clear objects in reach array
	ObjectsInReach.Empty();

	// Start grasp event
	//UMCFixationGraspController::StartGraspEvent(FixatedObject);
}

// Detach fixation
void UMCFixationGraspController::TryToDetach()
{
	if (FixatedObject)
	{
		// Get current velocity before detachment (gets reseted)
		const FVector CurrVel = FixatedObject->GetVelocity();

		// Detach object from hand
		UStaticMeshComponent* SMC = FixatedObject->GetStaticMeshComponent();
		SMC->DetachFromComponent(FDetachmentTransformRules(
			EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, true));

		// Enable physics with and apply current hand velocity, clear pointer to object
		SMC->SetSimulatePhysics(true);
		SMC->bGenerateOverlapEvents = true;
		SMC->SetPhysicsLinearVelocity(CurrVel);
		
		// Clear fixate object reference
		FixatedObject = nullptr;
		
		// Enable and update overlaps
		bGenerateOverlapEvents = true;
		UpdateOverlaps();

		// Finish grasp event
		//UMCFixationGraspController::FinishGraspEvent(FixatedObject);
	}
}

// Check if object is graspable
bool UMCFixationGraspController::CanBeGrasped(AStaticMeshActor* InSMA)
{
	// Check if the object is movable
	if (!InSMA->IsRootComponentMovable())
	{
		return false;
	}

	// Check if actor has a static mesh component
	if (UStaticMeshComponent* SMC = InSMA->GetStaticMeshComponent())
	{
		// Check if component has physics on
		if (!SMC->IsSimulatingPhysics())
		{
			return false;
		}

		// Check if object fits size
		if (SMC->GetMass() < ObjectMaxMass
			&& InSMA->GetComponentsBoundingBox().GetSize().Size() < ObjectMaxLength)
		{
			return true;
		}
	}
	return false;
}

// Function called when an item enters the fixation overlap area
void UMCFixationGraspController::OnFixationGraspAreaBeginOverlap(class UPrimitiveComponent* HitComp, class AActor* OtherActor,
	class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult)
{
	if (AStaticMeshActor* OtherSMA = Cast<AStaticMeshActor>(OtherActor))
	{
		ObjectsInReach.Emplace(OtherSMA);
	}
}

// Function called when an item leaves the fixation overlap area
void UMCFixationGraspController::OnFixationGraspAreaEndOverlap(class UPrimitiveComponent* HitComp, class AActor* OtherActor,
	class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// Remove actor from array (if present)
	if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(OtherActor))
	{
		ObjectsInReach.Remove(SMA);
	}
}
