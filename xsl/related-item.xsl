<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    version="1.0">
    <xsl:template match="/">
        <mods>
            <xsl:attribute name="xmlns">http://www.loc.gov/mods/v3</xsl:attribute>
                <relatedItem>
                    <xsl:attribute name="type">host</xsl:attribute>
                    <xsl:copy-of select="/"/>
                </relatedItem>
        </mods>
    </xsl:template>
</xsl:stylesheet>