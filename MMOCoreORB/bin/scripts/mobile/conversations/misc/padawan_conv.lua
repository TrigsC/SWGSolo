-- Define the Template Object
padawanConvoTemplate = ConvoTemplate:new {
    initialScreen = "init", -- Must match the screen ID below
    templateType = "Lua",
    luaClassHandler = "padawanConvoHandler", -- Must match the Object name in your Handler file
    screens = {}
}

-- Define the Init Screen
init = ConvoScreen:new {
    id = "init",
    leftDialog = "", 
    customDialogText = "System: AI Neural Link Established.", -- Text shown when you click him
    stopConversation = "true", -- Close window immediately (we just want the trigger)
    options = {}
}

-- Add Screen to Template
padawanConvoTemplate:addScreen(init);

-- Register Template
-- CRITICAL FIX: Name String FIRST, Object SECOND
addConversationTemplate("padawanConvoTemplate", padawanConvoTemplate);