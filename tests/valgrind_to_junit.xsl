<?xml version="1.0" encoding="utf-8"?>
<!--
  XSLT stylesheet to convert Valgrind XML output to JUnit XML format.
  Based on the Jenkins xunit-plugin valgrind-1.0-to-junit.xsl.
-->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:template match="/">
    <testsuites>
      <testsuite>
        <xsl:attribute name="name">valgrind</xsl:attribute>
        <xsl:attribute name="tests">
          <xsl:value-of select="count(valgrindoutput/error) + 1"/>
        </xsl:attribute>
        <xsl:attribute name="failures">
          <xsl:value-of select="count(valgrindoutput/error)"/>
        </xsl:attribute>
        <xsl:attribute name="errors">0</xsl:attribute>

        <!-- One passing test case if no errors, to avoid empty suite -->
        <xsl:if test="count(valgrindoutput/error) = 0">
          <testcase name="valgrind-clean" classname="valgrind" time="0"/>
        </xsl:if>

        <xsl:apply-templates select="valgrindoutput/error"/>
      </testsuite>
    </testsuites>
  </xsl:template>

  <xsl:template match="error">
    <testcase>
      <xsl:attribute name="name">
        <xsl:value-of select="unique"/>
      </xsl:attribute>
      <xsl:attribute name="classname">
        <xsl:value-of select="kind"/>
      </xsl:attribute>
      <xsl:attribute name="time">0</xsl:attribute>
      <failure>
        <xsl:attribute name="type">
          <xsl:value-of select="kind"/>
        </xsl:attribute>
        <xsl:attribute name="message">
          <xsl:value-of select="xwhat/text | what"/>
        </xsl:attribute>
        <xsl:for-each select="stack">
          <xsl:for-each select="frame">
            <xsl:text>  at </xsl:text>
            <xsl:value-of select="fn"/>
            <xsl:if test="file">
              <xsl:text> (</xsl:text>
              <xsl:value-of select="file"/>
              <xsl:text>:</xsl:text>
              <xsl:value-of select="line"/>
              <xsl:text>)</xsl:text>
            </xsl:if>
            <xsl:if test="obj and not(file)">
              <xsl:text> (in </xsl:text>
              <xsl:value-of select="obj"/>
              <xsl:text>)</xsl:text>
            </xsl:if>
            <xsl:text>&#10;</xsl:text>
          </xsl:for-each>
          <xsl:text>&#10;</xsl:text>
        </xsl:for-each>
      </failure>
    </testcase>
  </xsl:template>
</xsl:stylesheet>
