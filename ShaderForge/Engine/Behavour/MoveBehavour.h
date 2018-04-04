#pragma once

#include "pch.h"
#include "Behavour.h"

namespace MarcusEngine {
	/// ��������
	namespace Mind {
		class MoveBehavour : public Behavour {
		protected:
			std::shared_ptr<MarcusEngine::Input::InputBase> m_input;
		public:
			MoveBehavour();
			void Execute() override;
		};
	}
}
