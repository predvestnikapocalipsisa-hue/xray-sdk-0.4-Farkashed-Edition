#include "stdafx.h"
#include <algorithm>

UIKeyForm::UIKeyForm() : m_AutoChange(true), m_TimeFactor(1), m_Position(0), m_currentEditMotion(nullptr)
{
}

UIKeyForm::~UIKeyForm()
{
}

void UIKeyForm::Draw()
{
    m_currentEditMotion = ATools->GetCurrentMotion();
    const float dpi = XrUIManager::GetUIScale();
    if (ImGui::Begin("KeyForm"))
    {
        float a, b, c;
        ATools->GetStatTime(a,b,c);
        if (AutoChange()) m_Position = c;
        ImGui::Checkbox("Auto", &m_AutoChange);
        ImGui::SameLine();
        ImGui::SetNextItemWidth((std::max)(80.f*dpi,ImGui::GetContentRegionAvail().x));
        ImGui::SliderFloat("##position", &m_Position,a,b,"%.4f");
        ImGui::SetNextItemWidth(120.f*dpi);
        ImGui::SliderFloat("LOD", &ATools->m_RenderObject.m_fLOD,0.f,1.f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.f*dpi);
        if (ImGui::SliderFloat("Time factor", &m_TimeFactor,0.f,1.f)) EDevice.time_factor(m_TimeFactor);
        if (ImGui::BeginTable("Motion marks",3,ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Mark",ImGuiTableColumnFlags_WidthFixed,64.f*dpi);
            ImGui::TableSetupColumn("Timeline",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions",ImGuiTableColumnFlags_WidthFixed,146.f*dpi);
            const char* labels[] = {"Left1","Right1","Left2","Right2"};
            const CAEPreferences* prefs = (CAEPreferences*)EPrefs;
            for (int id=0;id<4;++id)
            {
                const bool present = m_currentEditMotion && id < int(m_currentEditMotion->marks.size());
                const bool show = present || prefs->bAlwaysShowKeyBar34 || (id<2 && prefs->bAlwaysShowKeyBar12);
                if (!show) continue;
                ImGui::PushID(id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(labels[id]);
                ImGui::TableNextColumn();
                const int width = (std::max)(1,int(ImGui::GetContentRegionAvail().x));
                m_TempForPlotHistogram.resize(width);
                if (present) DrawMark(id);
                else std::fill(m_TempForPlotHistogram.begin(),m_TempForPlotHistogram.end(),0.f);
                ImGui::PlotHistogram("##mark",m_TempForPlotHistogram.data(),width,0,nullptr,0.f,1.f,
                    ImVec2(float(width),ImGui::GetFrameHeight()));
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!present);
                const float buttonWidth = (std::max)(1.f,(ImGui::GetContentRegionAvail().x-2*ImGui::GetStyle().ItemSpacing.x)/3.f);
                if (ImGui::Button("Del",ImVec2(buttonWidth,0))) SetMark(id,3);
                ImGui::SameLine();
                if (ImGui::Button("Up",ImVec2(buttonWidth,0))) SetMark(id,2);
                ImGui::SameLine();
                if (ImGui::Button("Down",ImVec2(buttonWidth,0))) SetMark(id,1);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
inline bool interval_comparer(const motion_marks::interval &i1, const motion_marks::interval &i2)
{
	return (i1.first < i2.first);
}
void UIKeyForm::SetMark(int id, int action)
{
	if (!m_currentEditMotion)
		return;

	if (id < 0 || id >= int(m_currentEditMotion->marks.size()))
		return;

	if (id < 0 || id >= int(m_currentEditMotion->marks.size())) return;
	motion_marks &M = m_currentEditMotion->marks[id];
	float a, b, c;
	ATools->GetStatTime(a, b, c);
	float cur_time = c - a;

	motion_marks::ITERATOR it = M.intervals.begin();
	motion_marks::ITERATOR it_e = M.intervals.end();

	if (action == 3)
	{ // del current

		for (; it != it_e; ++it)
		{
			motion_marks::interval &iv = *it;
			if (iv.first < cur_time && iv.second > cur_time)
			{
				M.intervals.erase(it);
				break;
			}
		}
	}
	else if (action == 2)
	{ // up
		for (; it != it_e; ++it)
		{
			motion_marks::interval &iv = *it;
			if (iv.first < cur_time && iv.second > cur_time)
			{
				iv.second = cur_time;
				break;
			}
		}
	}
	else if (action == 1)
	{ // down
		for (; it != it_e; ++it)
		{
			motion_marks::interval &iv = *it;
			if (iv.first < cur_time && iv.second > cur_time)
			{
				iv.first = cur_time;
				break;
			}
		}
		if (it == it_e)
		{ // insert new
			M.intervals.push_back(motion_marks::interval(cur_time, b - a));
		}
	}

	std::sort(M.intervals.begin(), M.intervals.end(), interval_comparer);
}

void UIKeyForm::DrawMark(int id)
{
	for (int i = 0; i < m_TempForPlotHistogram.size(); i++)
	{
		m_TempForPlotHistogram[i] = 0;
	}
	if (!m_currentEditMotion)
		return;
	if (id < 0 || id >= int(m_currentEditMotion->marks.size())) return;
	motion_marks &M = m_currentEditMotion->marks[id];

	float a, b, c;
	ATools->GetStatTime(a, b, c);
	float motion_length = b - a;
	if (!(motion_length > 0.f)) return;

	float k_len = m_TempForPlotHistogram.size() / motion_length;

	motion_marks::C_ITERATOR it = M.intervals.begin();
	motion_marks::C_ITERATOR it_e = M.intervals.end();

	for (; it != it_e; ++it)
	{
		const motion_marks::interval &iv = *it;
		Ivector2 posLT, posRB;
		for (int i = (std::max)(0,int(iv.first*k_len)); i < (std::min)(int(m_TempForPlotHistogram.size()),int(iv.second*k_len)); i++)
		{
			m_TempForPlotHistogram[i] = 1;
		}
	}
}
