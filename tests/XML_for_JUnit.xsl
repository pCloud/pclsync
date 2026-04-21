<?xml version="1.0" encoding="utf-8"?>
<!--
  XSLT stylesheet to convert Check XML output to JUnit XML format.
  Based on the Check project contrib/XML_for_JUnit.xsl.
  License: LGPL-2.1 (same as Check)
-->
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:c="http://check.sourceforge.net/ns"
    exclude-result-prefixes="c">
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:template match="/">
    <testsuites>
      <xsl:apply-templates select="c:testsuites/c:suite"/>
    </testsuites>
  </xsl:template>

  <xsl:template match="c:suite">
    <testsuite>
      <xsl:attribute name="name">
        <xsl:value-of select="c:title"/>
      </xsl:attribute>
      <xsl:attribute name="tests">
        <xsl:value-of select="count(c:test)"/>
      </xsl:attribute>
      <xsl:attribute name="failures">
        <xsl:value-of select="count(c:test[@result='failure'])"/>
      </xsl:attribute>
      <xsl:attribute name="errors">
        <xsl:value-of select="count(c:test[@result='error'])"/>
      </xsl:attribute>
      <xsl:attribute name="time">
        <xsl:value-of select="sum(c:test/c:duration)"/>
      </xsl:attribute>
      <xsl:apply-templates select="c:test"/>
    </testsuite>
  </xsl:template>

  <xsl:template match="c:test">
    <testcase>
      <xsl:attribute name="name">
        <xsl:value-of select="c:id"/>
      </xsl:attribute>
      <xsl:attribute name="classname">
        <xsl:value-of select="../c:title"/>
      </xsl:attribute>
      <xsl:attribute name="time">
        <xsl:value-of select="c:duration"/>
      </xsl:attribute>
      <xsl:if test="@result = 'failure'">
        <failure>
          <xsl:attribute name="message">
            <xsl:value-of select="c:message"/>
          </xsl:attribute>
          <xsl:value-of select="c:fn"/>:<xsl:value-of select="c:path"/>
        </failure>
      </xsl:if>
      <xsl:if test="@result = 'error'">
        <error>
          <xsl:attribute name="message">
            <xsl:value-of select="c:message"/>
          </xsl:attribute>
          <xsl:value-of select="c:fn"/>:<xsl:value-of select="c:path"/>
        </error>
      </xsl:if>
    </testcase>
  </xsl:template>
</xsl:stylesheet>
