#pragma once

#include "common.h"

namespace tv {

inline constexpr wchar_t kCommunityUrl[] = L"https://t.me/MapAdkillerr";
inline constexpr UINT kWechatQrResource = 103;
inline constexpr UINT kAlipayQrResource = 104;
inline constexpr UINT kWechatOriginalResource = 105;
inline constexpr UINT kAlipayOriginalResource = 106;
inline constexpr int kSupportFirstControl = 1101;

struct DonationAsset {
    UINT qr_resource;
    UINT original_resource;
    const wchar_t* filename;
    const wchar_t* title_key;
    const wchar_t* hint_key;
    const wchar_t* save_key;
};

const DonationAsset& donation_asset(int index);
bool save_donation_image(HWND owner, int index, bool& cancelled, std::wstring& error);
bool copy_community_link(HWND owner, std::wstring& error);
bool open_community(HWND owner, std::wstring& error);

} // namespace tv
