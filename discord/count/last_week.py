from discord.count.count_func import call_count
import json
import datetime


def recent():
    weekly_anime_list = json.load(open('../Trace/discord/data/daily_frequency.json', 'r'))
    week = {}
    last=sorted(weekly_anime_list.keys())[-1]
    temp = datetime.datetime.strptime(last,"%Y-%m-%d")
    for _ in range(7):

        date = temp.strftime("%Y-%m-%d")
    # for date, v in sorted(weekly_anime_list.items(), key=lambda x: x[0])[-7:]:
        temp = temp - datetime.timedelta(days=1)
        try:
            v = weekly_anime_list[date].items()
        except:
            continue
        for i, j in v:
            week[i] = week.get(i, 0) + j
    top_week = sorted(week.items(), key=lambda x: x[1], reverse=True)
    return top_week
