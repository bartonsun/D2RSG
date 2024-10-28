function getTemplateContents(races, scenarioSize, customParameters)
	local contents = {}

	if parameters then
		difficulty = customParameters[1]
		gameMode = customParameters[2]
	end

	contents.zones = getZones(races)
	contents.connections = getZoneConnections()

	return contents
end