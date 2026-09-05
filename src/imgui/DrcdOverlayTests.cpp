#include "DrcdOverlay.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    try
    {
        auto* context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.DisplaySize = {864,480}; io.DeltaTime = 1.0f/60;
        unsigned char* alpha; int aw, ah;
        io.Fonts->GetTexDataAsAlpha8(&alpha, &aw, &ah);
        io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(1));
        ImGui::NewFrame();
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->AddRectFilled({0,0}, {100,100}, IM_COL32(255,0,0,255));
        draw->PushClipRect({10,10}, {30,30}, true);
        draw->AddRectFilled({0,0}, {100,100}, IM_COL32(0,255,0,255));
        draw->PopClipRect();
        draw->AddRectFilled({120,0}, {220,100}, IM_COL32(255,255,255,128));
        draw->AddImage(reinterpret_cast<ImTextureID>(2), {240,0}, {340,100});
        draw->AddText({20,140}, IM_COL32_WHITE, "Cemu GamePad keyboard: abc 123");
        ImGui::Render();
        const auto start = std::chrono::steady_clock::now();
        auto rgb = DrcdOverlay::Rasterize(*ImGui::GetDrawData(), io.Fonts->TexID, alpha, aw, ah);
        auto pixel = [&](unsigned x, unsigned y, unsigned c) { return rgb[(y*864+x)*3+c]; };
        Check(rgb.size() == 864*480*3, "output size");
        Check(pixel(5,5,0)==255 && pixel(5,5,1)==0, "red fill");
        Check(pixel(20,20,0)==0 && pixel(20,20,1)==255, "clip interior");
        Check(pixel(35,20,0)==255 && pixel(35,20,1)==0, "clip exterior");
        Check(pixel(150,50,0)>=139 && pixel(150,50,0)<=141, "alpha blend");
        Check(pixel(170,50,0)==pixel(150,50,0), "shared-edge alpha seam");
        Check(pixel(250,50,0)==24, "unrelated texture ignored");
        unsigned glyphPixels=0;
        for (unsigned y=140;y<158;++y) for(unsigned x=20;x<300;++x) glyphPixels += pixel(x,y,0)>24;
        Check(glyphPixels>100, "font glyph rasterization");
        // Offset display origin must not move the image relative to its viewport.
        auto* data=ImGui::GetDrawData();
        for (int i=0;i<data->CmdListsCount;++i)
        {
            for (auto& vertex:data->CmdLists[i]->VtxBuffer) {vertex.pos.x+=100;vertex.pos.y+=50;}
            for (auto& cmd:data->CmdLists[i]->CmdBuffer) {cmd.ClipRect.x+=100;cmd.ClipRect.z+=100;cmd.ClipRect.y+=50;cmd.ClipRect.w+=50;}
        }
        data->DisplayPos={100,50};
        Check(DrcdOverlay::Rasterize(*data, io.Fonts->TexID, alpha, aw, ah)==rgb,"display origin");
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        io.NavInputs[ImGuiNavInput_DpadRight] = 1.0f;
        ImGui::NewFrame();
        Check(ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight), "legacy D-pad press imported");
        ImGui::Render();
        for (auto& value : io.NavInputs) value = 0;
        ImGui::NewFrame();
        Check(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight), "D-pad release not latched");
        ImGui::Render();
        const auto testTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        ImGui::NewFrame();
        draw=ImGui::GetBackgroundDrawList();
        draw->AddRectFilled({0,0},{864,480},IM_COL32(0,0,0,200));
        for (int row=0;row<5;++row) for(int col=0;col<12;++col)
        {
            ImVec2 p(20+col*68,180+row*52);
            draw->AddRectFilled(p,{p.x+60,p.y+44},IM_COL32(80,80,80,230));
            draw->AddText({p.x+20,p.y+12},IM_COL32_WHITE,"A");
        }
        ImGui::Render();
        const auto denseStart=std::chrono::steady_clock::now();
        auto dense=DrcdOverlay::Rasterize(*ImGui::GetDrawData(),io.Fonts->TexID,alpha,aw,ah);
        Check(dense.size()==864*480*3,"dense keyboard frame");
        std::cout << "overlay tests passed; fixtures_us=" << testTime << " dense_keyboard_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-denseStart).count() << '\n';
        ImGui::DestroyContext(context);
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
