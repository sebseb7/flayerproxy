/**
 * Caches scoreboard state: objectives, display slots, scores, and teams.
 */
class ScoreboardCache {
  constructor() {
    this.objectives = new Map();     // name -> objective data
    this.displays = new Map();       // position -> display data
    this.scores = new Map();         // "name:objective" -> score data
    this.teams = new Map();          // name -> team data
  }

  handleScoreboardObjective(data) {
    if (data.action === 1) {
      // Remove objective
      this.objectives.delete(data.name);
    } else {
      this.objectives.set(data.name, { ...data });
    }
  }

  handleScoreboardDisplayObjective(data) {
    this.displays.set(data.position, { ...data });
  }

  handleScoreboardScore(data) {
    const key = `${data.itemName || data.entity}:${data.scoreName || data.objective}`;
    this.scores.set(key, { ...data });
  }

  handleResetScore(data) {
    const key = `${data.itemName || data.entity}:${data.scoreName || data.objective}`;
    this.scores.delete(key);
  }

  handleTeams(data) {
    if (data.mode === 1) {
      // Remove team
      this.teams.delete(data.team);
    } else {
      const existing = this.teams.get(data.team) || {};
      this.teams.set(data.team, { ...existing, ...data });
    }
  }

  /**
   * @returns {Array<{name: string, data: object}>}
   */
  getReplayPackets() {
    const packets = [];
    for (const [, obj] of this.objectives) {
      packets.push({ name: 'scoreboard_objective', data: obj });
    }
    for (const [, display] of this.displays) {
      packets.push({ name: 'scoreboard_display_objective', data: display });
    }
    for (const [, score] of this.scores) {
      packets.push({ name: 'scoreboard_score', data: score });
    }
    for (const [, team] of this.teams) {
      packets.push({ name: 'teams', data: team });
    }
    return packets;
  }

  clear() {
    this.objectives.clear();
    this.displays.clear();
    this.scores.clear();
    this.teams.clear();
  }
}

module.exports = { ScoreboardCache };
