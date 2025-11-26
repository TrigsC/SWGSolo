-- Define the Template
padawanConvoTemplate = ConvoTemplate:new {
    initialScreen = "init_chat",
    templateType = "Lua",
    luaClassHandler = "padawanConvHandler",
    screens = {}
}

-- Define the Init Screen (What he says when you click him)
init_chat = ConvoScreen:new {
    id = "init_chat",
    leftDialog = "", -- We will set this dynamically
    customDialogText = "I am listening, Master. (AI System Online)",
    stopConversation = "true", -- Close the window immediately, we just needed the trigger
    options = {}
}

padawanConvoTemplate:addScreen(init_chat);

-- Register the Template
addConversationTemplate(padawanConvoTemplate, "padawanConvoTemplate");