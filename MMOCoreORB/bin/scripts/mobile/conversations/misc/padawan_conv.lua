-- Define the Template Object
padawanConvoTemplate = ConvoTemplate:new {
    initialScreen = "padawan_init", -- Must match the screen ID below
    templateType = "Lua",
    luaClassHandler = "padawanConvoHandler", -- Must match the Object name in your Handler file
    screens = {}
}

-- Define the Init Screen
padawan_init = ConvoScreen:new {
    id = "padawan_init",
    leftDialog = "", 
    customDialogText = "System: AI Neural Link Established.", -- Text shown when you click him
    stopConversation = "true", -- Close window immediately (we just want the trigger)
    options = {}
}

-- Add Screen to Template
padawanConvoTemplate:addScreen(padawan_init);

-- Register Template
-- CRITICAL FIX: Name String FIRST, Object SECOND
addConversationTemplate("padawanConvoTemplate", padawanConvoTemplate);