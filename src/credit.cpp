#include "credit.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "hud.h"
#include "log.h"
#include "story.h"

#include <algorithm>
#include <cstdio>

using namespace game;

namespace credit {
namespace {

constexpr int kVarOwed = 3452;  // no script reads or writes it (it's in a run of 8 like it)
constexpr int kDownShare = 4;   // a quarter down at least
constexpr uint8_t kPickupAssetRevenue = 16;
// CPickup::Update's CHud::SetHelpMessage call for a for-sale sign ("Press ...
// to purchase ... for $~1~"), as a return address.
constexpr uintptr_t kForSaleHelpSite = 0x75624;

enum class Offer { kNone, kCredit, kShort, kOwing };
Offer gOffer;  // at the sign being updated
int32_t gRest; // credit: what he'd owe; short: the down payment it takes; owing: what he owes

bool gInstalled;
bool (*orig_PickupUpdate)(uint8_t *pickup, uint8_t *ped, uint8_t *vehicle, int player);
void (*orig_SetHelpMessage)(uint16_t *text, bool quick, bool forever);

int32_t &Money() { return *reinterpret_cast<int32_t *>(const_cast<uint8_t *>(PlayerInfo()) + 0xc8); }

// The sign shows its price and, on the buy button, takes it if he has it
// (CPickup::Update, PICKUP_PROPERTY_FORSALE). On credit it asks for the cash
// he has instead, for as long as it's updated: the help text then says so
// (Hook_SetHelpMessage adds the rest), and if the money went, he bought it.
bool Hook_PickupUpdate(uint8_t *pickup, uint8_t *ped, uint8_t *vehicle, int player)
{
	if (!gConfig.credit || !story::SoldAt(pickup))
		return orig_PickupUpdate(pickup, ped, vehicle, player);
	int32_t &price = *reinterpret_cast<int32_t *>(pickup + 0x20); // m_nQuantity
	const int32_t full = price, money = Money(), owed = Owed();
	if (money >= full)
		return orig_PickupUpdate(pickup, ped, vehicle, player);
	if (owed > 0) {
		gOffer = Offer::kOwing;
		gRest = owed;
	} else if (money * kDownShare < full) {
		gOffer = Offer::kShort;
		gRest = (full + kDownShare - 1) / kDownShare;
	} else {
		gOffer = Offer::kCredit;
		gRest = full - money;
		price = money;
	}
	const bool collected = orig_PickupUpdate(pickup, ped, vehicle, player);
	if (gOffer == Offer::kCredit) {
		price = full;
		if (Money() == 0) {
			ScriptGlobal(kVarOwed) = gRest;
			Log("credit: bought %s for $%d down, $%d owed", story::SoldAt(pickup)->name, money, gRest);
		}
	}
	gOffer = Offer::kNone;
	return collected;
}

void Hook_SetHelpMessage(uint16_t *text, bool quick, bool forever)
{
	const uintptr_t site =
	    reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - reinterpret_cast<uintptr_t>(hooks::ImageBase());
	if (gOffer == Offer::kNone || !text || site != kForSaleHelpSite) {
		orig_SetHelpMessage(text, quick, forever);
		return;
	}
	char money[24], more[160];
	hud::FormatMoney(gRest, money, sizeof money);
	switch (gOffer) {
	case Offer::kCredit: snprintf(more, sizeof more, " on credit. Your businesses' income pays off the other %s.", money); break;
	case Offer::kShort: snprintf(more, sizeof more, ". With %s you could buy it on credit.", money); break;
	default: snprintf(more, sizeof more, ". You still owe %s on credit.", money); break;
	}
	uint16_t wide[160], joined[256]; // CHud keeps 256
	api.AsciiToUnicode(more, wide);
	size_t n = 0;
	for (const uint16_t *s = text; *s && n + 1 < std::size(joined); s++)
		joined[n++] = *s;
	for (const uint16_t *s = wide; *s && n + 1 < std::size(joined); s++)
		joined[n++] = *s;
	joined[n] = 0;
	orig_SetHelpMessage(joined, quick, forever);
}

} // namespace

int32_t Owed() { return gInstalled ? std::max(0, ScriptGlobal(kVarOwed)) : 0; }

void Update()
{
	if (!gInstalled)
		return;
	int32_t &owed = ScriptGlobal(kVarOwed);
	if (owed <= 0)
		return;
	for (int i = 0; i < kNumPickups && owed > 0; i++) {
		uint8_t *p = api.pickups + size_t(i) * 64;
		if (p[0x36] != kPickupAssetRevenue || p[0x37] /* removed */)
			continue;
		float &revenue = *reinterpret_cast<float *>(p + 0xc); // m_fRevenue
		const int32_t take = std::min(owed, int32_t(revenue));
		if (take > 0) {
			revenue -= float(take);
			owed -= take;
		}
	}
	if (owed == 0)
		Log("credit: paid off");
}

bool Install(bool scriptKnown)
{
	if (!gConfig.credit)
		return true;
	if (!scriptKnown)
		return false;
	gInstalled =
	    hooks::Install("_ZN7CPickup6UpdateEP10CPlayerPedP8CVehiclei", Hook_PickupUpdate, orig_PickupUpdate) &&
	    hooks::Install("_ZN4CHud14SetHelpMessageEPtbb", Hook_SetHelpMessage, orig_SetHelpMessage);
	return gInstalled;
}

} // namespace credit
