#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessViewContext.h>

class plUiStyleContext;

class plUiStyleViewContext : public plEngineProcessViewContext
{
public:
  plUiStyleViewContext(plUiStyleContext* pUiStyleContext);
  ~plUiStyleViewContext();

protected:
  virtual plViewHandle CreateView() override;

  plUiStyleContext* m_pUiStyleContext;
};
